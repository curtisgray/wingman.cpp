// ReSharper disable CppInconsistentNaming
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include "llama.hpp"
#include "owned_cstrings.h"
#include "curl.h"
#include "types.h"

namespace wingman::tools {
	enum Role {
		system,
		assistant,
		user
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(Role, {
		{ Role::system, "system" },
		{ Role::assistant, "assistant" },
		{ Role::user, "user" }
	})

		struct Message {
		std::string role;
		std::string content;
	};

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, role, content)

	using AnnoyIndex = Annoy::AnnoyIndex<size_t, float, Annoy::Angular, Annoy::Kiss32Random, Annoy::AnnoyIndexMultiThreadedBuildPolicy>;

	orm::ItemActionsFactory actions_factory;
	WingmanItemStatus inferenceStatus;

	bool OnInferenceProgressDefault(const nlohmann::json &metrics)
	{
		return true;
	}

	bool OnInferenceProgress(const nlohmann::json &metrics)
	{
		return true;
	}

	void OnInferenceStatusDefault(const std::string &alias, const WingmanItemStatus &status)
	{
		inferenceStatus = status;
	}

	void OnInferenceStatus(const std::string &alias, const WingmanItemStatus &status)
	{
		inferenceStatus = status;
		auto wi = actions_factory.wingman()->get(alias);
		if (wi) {
			wi.value().status = status;
			actions_factory.wingman()->set(wi.value());
		} else {
			spdlog::error(" ***(OnInferenceStatus) Alias {} not found***", alias);
		}
	}

	void OnInferenceServiceStatusDefault(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{}

	void OnInferenceServiceStatus(const WingmanServiceAppItemStatus &status, const std::optional<std::string> &error)
	{
		auto appItem = actions_factory.app()->get("WingmanService").value_or(AppItem::make("WingmanService"));

		nlohmann::json j = nlohmann::json::parse(appItem.value);
		auto wingmanServerItem = j.get<WingmanServiceAppItem>();
		wingmanServerItem.status = status;
		if (error) {
			wingmanServerItem.error = error;
		}
		nlohmann::json j2 = wingmanServerItem;
		appItem.value = j2.dump();
		actions_factory.app()->set(appItem);
	}

	std::tuple<std::shared_ptr<ModelLoader>, std::shared_ptr<ModelLoader>> InitializeLoaders()
	{
		const std::vector<std::string> models = {
			// "jinaai[-]jina-embeddings-v2-base-en[=]jina-embeddings-v2-base-en-f16.gguf",
			// "BAAI[-]bge-large-en-v1.5[=]bge-large-en-v1.5-Q8_0.gguf",
			// "TheBloke[-]phi-2-dpo-GGUF[=]phi-2-dpo.Q4_K_S.gguf",
			"second-state[-]All-MiniLM-L6-v2-Embedding-GGUF[=]all-MiniLM-L6-v2-Q5_K_M.gguf",
			"TheBloke[-]CapybaraHermes-2.5-Mistral-7B-GGUF[=]capybarahermes-2.5-mistral-7b.Q4_K_S.gguf"
		};
		std::vector<std::shared_ptr<ModelLoader>> loaders;
		std::map<std::string, std::string> options;

		bool first = true;
		for (const auto &model : models) {
			if (first) {
				auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
				loaders.push_back(loader);
				first = false;
			} else {
				// auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgress, OnInferenceStatus, OnInferenceServiceStatus);
				auto loader = std::make_shared<ModelLoader>(model, OnInferenceProgressDefault, OnInferenceStatusDefault, OnInferenceServiceStatusDefault);
				loaders.push_back(loader);
			}
		}

		return { loaders[0], loaders[1] };
	}

	const ModelGenerator::token_callback onNewToken = [](const std::string &token) {
		std::cout << token;
	};

	void Generate(const ModelGenerator &generator, const std::string &prompt, const bool isRetrieval)
	{
		gpt_params params;
		int maxTokensToGenerate;
		if (isRetrieval) {
			maxTokensToGenerate = 512;
			// For BERT models, batch size must be equal to ubatch size
			// ReSharper disable once CppLocalVariableMightNotBeInitialized
			params.n_ubatch = params.n_batch;
			params.embedding = true;
		} else {
			maxTokensToGenerate = 1024;
		}
		params.prompt = prompt;

		constexpr std::atomic_bool tokenGenerationCancelled = false;
		std::cout << "Generating tokens for model: " << generator.modelName() << std::endl;
		generator.generate(params, maxTokensToGenerate, onNewToken, tokenGenerationCancelled);
		std::cout << std::endl;
	};

	void Generate(const ModelGenerator &generator, const char *promptStr, const bool isRetrieval)
	{
		const std::string prompt = promptStr;
		Generate(generator, prompt, isRetrieval);
	}

	std::optional<nlohmann::json> sendRetreiverRequest(const std::string &query, const int port = 45678)
	{
		nlohmann::json response;
		std::string response_body;
		bool success = false;
		// Initialize curl globally
		curl_global_init(CURL_GLOBAL_DEFAULT);

		// Initialize curl handle
		if (const auto curl = curl_easy_init()) {
			// Set the URL for the POST request
			curl_easy_setopt(curl, CURLOPT_URL, ("http://localhost:" + std::to_string(port) + "/embedding").c_str());

			// Specify the POST data
			// first wrap the query in a json object
			const nlohmann::json j = {
				{ "input", query }
			};
			const std::string json = j.dump();
			const size_t content_length = json.size();
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

			// Set the Content-Type header
			struct curl_slist *headers = nullptr;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(content_length)).c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			const auto writeFunction = +[](void *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				const auto body = static_cast<std::string *>(userdata);
				const auto bytes = static_cast<std::byte *>(contents);
				const auto numBytes = size * nmemb;
				body->append(reinterpret_cast<const char *>(bytes), numBytes);
				return size * nmemb;
			};
			// Set write callback function to append data to response_body
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
			} else {
				// Parse the response body as JSON
				response = nlohmann::json::parse(response_body);
				success = true;
			}
		}

		// Cleanup curl globally
		curl_global_cleanup();

		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	// Function to call the OpenAI API completion endpoint with a prompt and handle SSE
	std::optional<nlohmann::json> sendChatCompletionRequest(const std::vector<Message> &messages, const std::string &modelName, const int port = 45679)
	{
		nlohmann::json response;
		std::string response_body;
		bool success = false;
		const std::string url = "http://localhost:" + std::to_string(port) + "/v1/chat/completions";

		curl_global_init(CURL_GLOBAL_ALL);
		if (const auto curl = curl_easy_init()) {
			const auto headerCallback = +[](const char *contents, const size_t size, const size_t nmemb, void *userp) -> size_t {
				const auto numBytes = size * nmemb;
				return numBytes;
			};
			const auto eventCallback = +[](const char *contents, const size_t size, const size_t nmemb, void *userdata) -> size_t {
				// Extract the event data and display it
				const std::string event_data(contents, size * nmemb);
				// std::cout << event_data << std::endl;
				const auto j = nlohmann::json::parse(event_data);
				auto completion = j["choices"][0]["message"]["content"].get<std::string>();
				// look for `<|im_end|>` in the completion and remove it
				const auto end_pos = completion.find("<|im_end|>");
				if (end_pos != std::string::npos) {
					completion.erase(end_pos);
				}
				std::cout << completion << std::endl;

				const auto body = static_cast<std::string *>(userdata);
				const auto numBytes = size * nmemb;
				body->append(contents, numBytes);

				return numBytes;
			};

			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

			// Create the JSON object to send to the API
			nlohmann::json j;
			j["messages"] = messages;
			j["model"] = modelName;
			j["max_tokens"] = 100;
			j["temperature"] = 0.7;

			const std::string json = j.dump();
			const size_t content_length = json.size();
			struct curl_slist *headers = nullptr;
			// headers = curl_slist_append(headers, ("Authorization: Bearer " + API_KEY).c_str());
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(content_length)).c_str());

			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, eventCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

			// Set up curl for receiving SSE events
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);

			// Perform the request
			const CURLcode res = curl_easy_perform(curl);

			// Check for errors
			if (res != CURLE_OK) {
				std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
			} else {
				// Parse the response body as JSON
				response = nlohmann::json::parse(response_body);
				success = true;
			}
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
		}

		curl_global_cleanup();
		if (!success) {
			return std::nullopt;
		}
		return response;
	}

	std::tuple<std::function<void()>, std::thread> StartRetreiver(ModelLoader &retriever, const int port)
	{
		std::cout << "Retrieving with model: " << retriever.modelName() << std::endl;

		const auto filename = std::filesystem::path(retriever.getModelPath()).filename().string();
		const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
		if (!dli) {
			std::cerr << "Failed to parse download item name from safe file path" << std::endl;
			return {};
		}
		std::map<std::string, std::string> options;
		options["--port"] = std::to_string(port);
		options["--model"] = retriever.getModelPath();
		options["--alias"] = dli.value().filePath;
		options["--gpu-layers"] = "4";
		options["--embedding"] = "";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		args.emplace_back("retrieve");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			if (!value.empty()) {
				args.push_back(value);
			}
		}
		owned_cstrings cargs(args);
		std::function<void()> requestShutdownInference;
		inferenceStatus = WingmanItemStatus::unknown;
		std::thread inferenceThread([&retriever, &cargs, &requestShutdownInference]() {
			retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		});

		while (inferenceStatus != WingmanItemStatus::inferring) {
			fmt::print("{}: {}\t\t\t\r", retriever.modelName(), WingmanItem::toString(inferenceStatus));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << std::endl;
		// retriever.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		// retriever.retrieve(query);
		return { std::move(requestShutdownInference), std::move(inferenceThread) };
	}

	std::tuple<std::function<void()>, std::thread> StartGenerator(const ModelLoader &generator, const int port)
	{
		std::cout << "Generating with model: " << generator.modelName() << std::endl;

		const auto filename = std::filesystem::path(generator.getModelPath()).filename().string();
		const auto dli = actions_factory.download()->parseDownloadItemNameFromSafeFilePath(filename);
		if (!dli) {
			std::cerr << "Failed to parse download item name from safe file path" << std::endl;
			return {};
		}
		std::map<std::string, std::string> options;
		options["--port"] = std::to_string(port);
		options["--model"] = generator.getModelPath();
		options["--alias"] = dli.value().filePath;
		options["--gpu-layers"] = "99";

		// join pairs into a char** argv compatible array
		std::vector<std::string> args;
		args.emplace_back("generate");
		for (const auto &[option, value] : options) {
			args.push_back(option);
			if (!value.empty()) {
				args.push_back(value);
			}
		}
		owned_cstrings cargs(args);
		std::function<void()> requestShutdownInference;
		inferenceStatus = WingmanItemStatus::unknown;
		std::thread inferenceThread([&generator, &cargs, &requestShutdownInference]() {
			generator.run(static_cast<int>(cargs.size() - 1), cargs.data(), requestShutdownInference);
		});

		while (inferenceStatus != WingmanItemStatus::inferring) {
			fmt::print("{}: {}\t\t\t\r", generator.modelName(), WingmanItem::toString(inferenceStatus));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		std::cout << std::endl;
		return { std::move(requestShutdownInference), std::move(inferenceThread) };
	};

	// Function to store embeddings in Annoy index
	void storeEmbeddings(const std::string &annoyFilePath, const std::vector<std::vector<float>> &embeddings)
	{
		const size_t embeddingDim = embeddings[0].size();
		AnnoyIndex annoyIndex(embeddingDim);

		// Add embeddings to the index
		for (size_t i = 0; i < embeddingDim; ++i) {
			annoyIndex.add_item(i, &embeddings[0][i]);
		}

		// Build the index
		annoyIndex.build(10); // 10 trees

		// Save the index to disk
		annoyIndex.save(annoyFilePath.c_str());
	}

	// Function to retrieve data based on embedding input
	std::vector<size_t> retrieveData(const std::string &annoyFilePath, const std::vector<float> &queryEmbedding, const int numNeighbors = 10)
	{
		// Load the Annoy index from float
		AnnoyIndex annoyIndex(queryEmbedding.size());
		annoyIndex.load(annoyFilePath.c_str());

		// Retrieve nearest neighbors
		std::vector<size_t> neighborIndices;
		std::vector<float> distances;
		annoyIndex.get_nns_by_vector(queryEmbedding.data(), numNeighbors, -1, &neighborIndices, &distances);

		return neighborIndices;
	}

	const char *GetCreateEmbeddingTableSQL()
	{
		return "CREATE TABLE IF NOT EXISTS embeddings ("
			"id INTEGER PRIMARY KEY, "
			"chunk TEXT, "
			"embedding BLOB, "
			"source TEXT, "
			"created INTEGER DEFAULT (unixepoch('now')) NOT NULL)";
	}

	std::optional<sqlite3 *> OpenEmbeddingDatabase(const std::string &dbPath)
	{
		sqlite3 *db = nullptr;
		std::optional<sqlite3 *> dbPtr;
		char *errMsg = nullptr;

		int rc = sqlite3_open(dbPath.c_str(), &db);
		if (rc) {
			std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
			dbPtr = std::nullopt;
		} else {
			const auto createTableSQL = GetCreateEmbeddingTableSQL();
			rc = sqlite3_exec(db, createTableSQL, nullptr, nullptr, &errMsg);
			if (rc != SQLITE_OK) {
				std::cerr << "SQL error: " << errMsg << std::endl;
				dbPtr = std::nullopt;
			} else {
				dbPtr = db;
			}
		}
		if (errMsg) {
			sqlite3_free(errMsg);
		}
		if (dbPtr.has_value())
			return dbPtr;
		sqlite3_close(db);
		return std::nullopt;
	}

	void CloseEmbeddingDatabase(sqlite3 *db)
	{
		sqlite3_close(db);
	}

	std::optional<AnnoyIndex> LoadAnnoyIndex(const std::string &annoyFilePath)
	{
		AnnoyIndex annoyIndex(1);
		// if (std::filesystem::exists(annoyFilePath) && std::filesystem::is_regular_file(annoyFilePath)) {
		// 	if (annoyIndex.load(annoyFilePath.c_str())) {
		// 		return annoyIndex;
		// 	}
		// }
		annoyIndex.on_disk_build(annoyFilePath.c_str());
		return annoyIndex;
	}

	bool AddEmbeddingToAnnoy(AnnoyIndex &annoyIndex, const size_t id, const std::vector<float> &embedding)
	{
		try {
			annoyIndex.add_item(id, embedding.data());
			return true;
		} catch (const std::exception &e) {
			std::cerr << "Failed to add item to Annoy index: " << e.what() << std::endl;
			return false;
		}
	}

	size_t InsertEmbedding(sqlite3 *db, AnnoyIndex &annoyIndex, const std::string &chunk, const std::string &source, const std::vector<float> &embedding)
	{
		sqlite3_stmt *stmt = nullptr;
		size_t id = -1;

		// Begin a transaction
		int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to begin transaction: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		const std::string insertSQL = "INSERT INTO embeddings (chunk, source, embedding) VALUES (?, ?, ?)";

		rc = sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &stmt, nullptr);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		// Bind the text chunk
		rc = sqlite3_bind_text(stmt, 1, chunk.c_str(), -1, SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to bind text chunk: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		// Bind the source
		rc = sqlite3_bind_text(stmt, 2, source.c_str(), -1, SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to bind source: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		// Bind the embedding
		rc = sqlite3_bind_blob(stmt, 3, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to bind embedding: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		// Execute the statement
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			sqlite3_reset(stmt);
			std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
			return id;
		}

		sqlite3_finalize(stmt);

		id = static_cast<size_t>(sqlite3_last_insert_rowid(db));

		const bool added = AddEmbeddingToAnnoy(annoyIndex, id, embedding);

		if (!added) {
			// Rollback the transaction
			rc = sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
			if (rc != SQLITE_OK) {
				std::cerr << "Failed to rollback transaction: " << sqlite3_errmsg(db) << std::endl;
			}
			return -1;
		}

		// Commit the transaction
		rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
		if (rc != SQLITE_OK) {
			std::cerr << "Failed to commit transaction: " << sqlite3_errmsg(db) << std::endl;
			return -1;
		}

		return id;
	}

	// Five paragraphs of Lorem Ipsum text as a vector, one per paragraph, for testing
	std::vector<std::tuple<std::string, std::string>> GetLoremIpsumText()
	{
		return {
			{
				"At vero eos et accusamus et iusto odio dignissimos ducimus qui blanditiis praesentium voluptatum deleniti atque corrupti quos dolores et quas molestias excepturi sint occaecati cupiditate non provident, similique sunt in culpa qui officia deserunt mollitia animi, id est laborum et dolorum fuga.",
				"test-data"
				},
			{
				"Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio cumque nihil impedit quo minus id quod maxime placeat facere possimus, omnis voluptas assumenda est, omnis dolor repellendus.",
				"test-data"
			},
			{
				"Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat.",
				"test-data"
			},
			{
				"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.",
				"test-data"
			},
			{
				"Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.",
				"test-data"
			}
		};
	}

	void Start()
	{
		const auto home = GetWingmanHome();
		const std::string annoyFilePath = (home / "data" / std::filesystem::path("embeddings.ann").filename().string()).string();
		const std::string dbPath = (home / "data" / std::filesystem::path("embeddings.db").filename().string()).string();

		auto [retriever, generator] = InitializeLoaders();
		std::cout << "Retriever model: " << retriever->modelName() << std::endl;
		auto [retrieverShutdown, retrieverThread] = StartRetreiver(*retriever, 45678);


		const auto storageQueries = GetLoremIpsumText();

		// Store embeddings in Sqlite/Annoy db/index
		auto db = OpenEmbeddingDatabase(dbPath);
		if (db) {
			auto annoyIndex = LoadAnnoyIndex(annoyFilePath);
			if (annoyIndex) {
				for (const auto &[chunk, source] : storageQueries) {
					auto rtrResp = sendRetreiverRequest(chunk);
					if (rtrResp) {
						std::cout << "Response: " << rtrResp.value() << std::endl;
					} else {
						throw std::runtime_error("Failed to retrieve response");
					}
					std::vector<float> storageEmbedding;
					nlohmann::json jr = rtrResp.value()["data"][0];
					for (const auto &element : jr["embedding"]) {
						storageEmbedding.push_back(element.get<float>());
					}
					// Store embeddings in Sqlite db
					const size_t id = InsertEmbedding(db.value(), annoyIndex.value(), chunk, source, storageEmbedding);
					if (id != -1)
						std::cout << "Inserted embedding with id: " << id << std::endl;
					else
						throw std::runtime_error("Failed to insert embedding");
				}
			} else {
				throw std::runtime_error("Failed to load Annoy index");
			}
		}



		// auto retreivalQuery = "sint occaecati cupiditate non provident";
		// auto rtrResp = sendRetreiverRequest(storageQueries[0]);
		// if (rtrResp) {
		// 	std::cout << "Response: " << rtrResp.value() << std::endl;
		// } else {
		// 	// std::cerr << "Failed to retrieve response" << std::endl;
		// 	throw std::runtime_error("Failed to retrieve response");
		// }
		// // convert json array to vector of floats
		// std::vector<float> storageEmbedding;
		// nlohmann::json jr = rtrResp.value()["data"][0];
		// for (const auto &element : jr["embedding"]) {
		// 	storageEmbedding.push_back(element.get<float>());
		// }
		// // Store embeddings in Annoy index
		// const std::vector<std::vector<float>> embeddings = { storageEmbedding };
		// storeEmbeddings(annoyFilePath, embeddings);
		//
		//
		// // std::cout << "Generator model: " << generator->modelName() << std::endl;
		// // auto [generatorShutdown, generatorThread] = StartGenerator(*generator, 45679);
		// //
		// // std::vector<Message> messages = {
		// // 	{ "system", "You are a helpful assistant." },
		// // 	{ "user", "Hello there, how are you?" },
		// // };
		// // auto genResp = sendChatCompletionRequest(messages, generator->modelName());
		// // if (genResp) {
		// // 	std::cout << "Response: " << genResp.value() << std::endl;
		// // 	// add response to messages
		// // 	messages.push_back({ "assistant", genResp.value()["choices"][0]["message"]["content"] });
		// // } else {
		// // 	// std::cerr << "Failed to retrieve response" << std::endl;
		// // 	throw std::runtime_error("Failed to retrieve response");
		// // }
		//
		//
		// rtrResp = sendRetreiverRequest(retreivalQuery);
		// if (rtrResp) {
		// 	std::cout << "Response: " << rtrResp.value() << std::endl;
		// } else {
		// 	throw std::runtime_error("Failed to retrieve response");
		// }
		// std::vector<float> queryEmbedding;
		// jr = rtrResp.value()["data"][0];
		// for (const auto &element : jr["embedding"]) {
		// 	queryEmbedding.push_back(element.get<float>());
		// }
		// auto retrievedIndices = retrieveData(annoyFilePath, queryEmbedding, 5);
		// std::cout << "Retrieved indices: ";
		// for (const auto &index : retrievedIndices) {
		// 	std::cout << index << " ";
		// }
		//
		//
		// // messages.push_back({ "user", ("Can you decode the following tokens? " + rtrResp.value()["embedding"].get<std::string>()) });
		// // genResp = sendChatCompletionRequest(messages, generator->modelName());
		// // if (genResp) {
		// // 	std::cout << "Response: " << genResp.value() << std::endl;
		// // } else {
		// // 	std::cerr << "Failed to retrieve response" << std::endl;
		// // }
		// // generatorShutdown();
		// // generatorThread.join();
		retrieverShutdown();
		retrieverThread.join();
	}
}

int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::trace);

	wingman::tools::Start();
}
