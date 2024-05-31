#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>

#include <csignal>

#include "exceptions.h"
#include "control.h"
#include "embedding.index.h"
#include "types.h"

namespace wingman::tools {

	struct Params {
		bool loadAI = false;
		std::string baseInputFilename = "embeddings";
		std::string query;
		std::string embeddingModel = "BAAI/bge-large-en-v1.5/bge-large-en-v1.5-Q8_0.gguf";
		// std::string inferenceModel = "MaziyarPanahi/Mistral-7B-Instruct-v0.3-GGUF/Mistral-7B-Instruct-v0.3.Q5_K_S.gguf";
		// std::string inferenceModel = "microsoft/Phi-3-mini-4k-instruct-gguf/Phi-3-mini-4k-instruct-q4.gguf";
		std::string inferenceModel;
		bool jsonOutput = false;
	};

	std::function<void(int)> shutdown_handler;
	orm::ItemActionsFactory actions_factory;
	bool requested_shutdown;

	void SIGINT_Callback(const int signal)
	{
		shutdown_handler(signal);
	}

	bool singleModel(const Params &params)
	{
		if (params.embeddingModel.empty() || params.inferenceModel.empty())
			return false;
		return util::stringCompare(params.embeddingModel, params.inferenceModel);
	}

	void printNearestNeighbors(const std::vector<silk::embedding::Embedding> &embeddings, const bool jsonOutput)
	{
		if (jsonOutput) {
			nlohmann::json silkContext;
			for (const auto &[record, distance] : embeddings) {
				silkContext.push_back({
					{ "id", record.id },
					{ "chunk", record.chunk },
					{ "source", record.source },
					{ "distance", distance }
				});
			}
			std::cout << silkContext.dump(4) << std::endl;
		} else {
			// Print the top 10 nearest neighbors
			std::cout << "Top 10 nearest neighbors:" << std::endl;
			for (size_t i = 0; i < std::min<size_t>(10, embeddings.size()); ++i) {
				const auto &[record, distance] = embeddings[i];
				std::cout << "Nearest neighbor " << i << ": Index=" << record.id << ", Angular Distance=" << distance << std::endl;
				std::cout << "   Chunk: " << record.chunk << std::endl;
				std::cout << "   Source: " << record.source << std::endl;
				std::cout << std::endl;
			}
		}
	}

	void Start(const Params &params)
	{
		const auto wingmanHome = GetWingmanHome();
		const std::string annoyFilePath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".ann").filename().string()).string();
		const std::string dbPath = (wingmanHome / "data" / std::filesystem::path(params.baseInputFilename + ".db").filename().string()).string();

		int controlPort = 6568;
		int embeddingPort = 6567;
		int inferencePort = 6567;
		if (params.loadAI) {
			controlPort = 45679;
			embeddingPort = 45678;
			inferencePort = 45677;
		}
		if (params.inferenceModel.empty()) {
			controlPort = 6568;
			inferencePort = 6567;
		}
		silk::control::ControlServer controlServer(controlPort, inferencePort);
		silk::embedding::EmbeddingAI embeddingAI(controlPort, embeddingPort, actions_factory);
		// wait for ctrl-c
		shutdown_handler = [&](int /* signum */) {
			requested_shutdown = true;
		};

		if (const auto res = std::signal(SIGINT, SIGINT_Callback); res == SIG_ERR) {
			spdlog::error(" (start) Failed to register signal handler.");
			return;
		}

		if (params.loadAI) {
			if (!params.inferenceModel.empty()) {
				controlServer.start();
				if (!controlServer.sendInferenceStartRequest(params.inferenceModel)) {
					throw std::runtime_error("Failed to start inference of control server");
				}
				// wait for 60 seconds while checking for the control server status to become
				//	healthy before starting the embedding AI
				bool isHealthy = false;
				for (int i = 0; i < 60; i++) {
					isHealthy = controlServer.sendInferenceHealthRequest();
					if (isHealthy) {
						break;
					}
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
				if (!isHealthy) {
					throw std::runtime_error("Inference server is not healthy");
				}
			}
			if (!singleModel(params))
				if (!embeddingAI.start(params.embeddingModel)) {
					throw std::runtime_error("Failed to start embedding AI");
				}
		}

		std::map<std::string, std::string> metadata;
		if (params.loadAI) {
			if (singleModel(params)) {
				const auto r = controlServer.sendRetrieveModelMetadataRequest();
				if (!r)
					throw std::runtime_error("Failed to retrieve model metadata");
				metadata = r.value()["metadata"];
			}
			else
				metadata = embeddingAI.ai->getMetadata();
		} else {
			const auto r = embeddingAI.sendRetrieveModelMetadataRequest();
			if (!r) {
				throw std::runtime_error("Failed to retrieve model metadata");
			}
			metadata = r.value()["metadata"];
		}
		int contextSize = 0;
		std::string bosToken;
		std::string eosToken;
		std::string modelName;
		if (metadata.empty())
			throw std::runtime_error("Failed to retrieve model metadata");
		if (metadata.contains("context_length")) {
			contextSize = std::stoi(metadata.at("context_length"));
			if (!params.jsonOutput)
				std::cout << "Embedding Context size: " << contextSize << std::endl;
		} else {
			throw std::runtime_error("Failed to retrieve model contextSize");
		}
		if (metadata.contains("tokenizer.ggml.bos_token_id")) {
			bosToken = metadata.at("tokenizer.ggml.bos_token_id");
			if (!params.jsonOutput)
				std::cout << "BOS token: " << bosToken << std::endl;
		} else {
			if (!params.jsonOutput)
				std::cout << "BOS token not found. Using empty string." << std::endl;
		}
		if (metadata.contains("tokenizer.ggml.eos_token_id")) {
			eosToken = metadata.at("tokenizer.ggml.eos_token_id");

			if (!params.jsonOutput)
				std::cout << "EOS token: " << eosToken << std::endl;
		} else {
			if (!params.jsonOutput)
				std::cout << "EOS token not found. Using empty string." << std::endl;
		}

		auto r = embeddingAI.sendRetrieverRequest(bosToken + "Hello world. This is a test." + eosToken);
		if (!r) {
			throw std::runtime_error("Getting dimensions: Failed to retrieve response");
		}
		auto s = silk::embedding::EmbeddingAI::extractEmbeddingFromJson(r.value());
		if (s.empty()) {
			throw std::runtime_error("Getting dimensions: Failed to extract embedding from response");
		}
		if (!params.jsonOutput)
			std::cout << "Embedding dimensions: " << s.size() << std::endl;

		size_t embeddingDimensions = s.size();

		silk::embedding::EmbeddingDb db(dbPath);
		silk::embedding::EmbeddingIndex embeddingIndex(params.baseInputFilename, static_cast<int>(embeddingDimensions));
		embeddingIndex.load();
		std::vector<silk::control::Message> messages;
		messages.emplace_back(silk::control::Message{ "system", "You are a friendly assistant." });
		disableInferenceLogging = true;
		while (true) {
			std::string query;
			if (params.query.empty()) {	// no query provided, ask user for input
				if (!params.jsonOutput) {
					printf("\n===========================================\n");
				}
				printf("Enter query (empty to quit): ");
				std::getline(std::cin, query);
				if (query.empty()) {
					break;
				}
			} else {
				query = params.query;
			}
			auto rtrResp = embeddingAI.sendRetrieverRequest(bosToken + query + eosToken);
			if (!rtrResp) {
				throw std::runtime_error("Failed to retrieve response");
			}

			const auto embeddings = embeddingIndex.getEmbeddings(rtrResp.value(), 10);

			if (!embeddings) {
				throw std::runtime_error("Failed to retrieve embeddings");
			}
			printNearestNeighbors(embeddings.value(), params.jsonOutput);
			nlohmann::json silkContext = embeddingIndex.getSilkContext(embeddings.value());
			if (!params.jsonOutput) {
				printf("\n===========================================\n");
			}
			// silk::control::OpenAIRequest request;
			// request.model = params.inferenceModel;
			// const auto queryContext = "Context:\n" + silkContext.dump(4) + "\n\n" + query;
			// messages.emplace_back(silk::control::Message{ "user", queryContext });
			// request.messages = nlohmann::json(messages);
			// const auto onChunk = [](const std::string &chunk) {
			// 	std::cout << "Chunk: " << chunk << std::endl;
			// };
			// if (!controlServer.sendChatCompletionRequest(request, onChunk)) {
			// 	// throw std::runtime_error("Failed to send chat completion request");
			// 	std::cerr << "Failed to send chat completion request" << std::endl;
			// }
			if (!params.query.empty()) {	// query provided, exit after processing it
				break;
			}
		}

		if (params.loadAI) {
			embeddingAI.stop();
			if (!params.inferenceModel.empty()) {
				if (controlServer.isInferenceRunning(params.inferenceModel))
					if (!controlServer.sendInferenceStopRequest(params.inferenceModel))
						std::cerr << "Failed to stop inference of control server" << std::endl;
				controlServer.stop();
			}
		}
	}

	static void ParseParams(int argc, char **argv, Params &params)
	{
		std::string arg;
		bool invalidParam = false;

		for (int i = 1; i < argc; i++) {
			arg = argv[i];
			if (arg == "--load-ai") {
				params.loadAI = true;
			} else if (arg == "--base-input-name") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.baseInputFilename = argv[i];
			} else if (arg == "--query") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.query = argv[i];
			} else if (arg == "--embedding-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.embeddingModel = argv[i];
			} else if (arg == "--inference-model") {
				if (++i >= argc) {
					invalidParam = true;
					break;
				}
				params.inferenceModel = argv[i];
			} else if (arg == "--json-output") {
				params.jsonOutput = true;
			} else if (arg == "--help" || arg == "-?" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				// std::cout << "  --input-path <path>     Path to the input directory or file" << std::endl;
				std::cout << "  --load-ai                   Load the AI model. Default: false" << std::endl;
				std::cout << "  --base-input-name <name>    Input file base name. Default: embeddings" << std::endl;
				std::cout << "  --query <query>             Query to run against the embeddings. Default: [ask user at runtime]" << std::endl;
				std::cout << "  --help, -?                  Show this help message" << std::endl;
				throw wingman::SilentException();
			} else {
				throw std::runtime_error("unknown argument: " + arg);
			}
		}

		if (invalidParam) {
			throw std::runtime_error("invalid parameter for argument: " + arg);
		}
	}
}


int main(int argc, char *argv[])
{
	spdlog::set_level(spdlog::level::info);
	auto params = wingman::tools::Params();

	ParseParams(argc, argv, params);
	wingman::tools::Start(params);
}
