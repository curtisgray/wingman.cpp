#include <chrono>
#include <thread>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "download.service.h"

DownloadService::DownloadService(wingman::ItemActionsFactory &actions_factory
		, const std::function<bool(wingman::curl::Response *)> &onDownloadProgress
		, const std::function<bool(wingman::DownloadServerAppItem *)> &onServiceStatus)
	: actions(actions_factory)
	, onDownloadProgress(onDownloadProgress)
	, onServiceStatus(onServiceStatus)
{}

void DownloadService::startDownload(const wingman::DownloadItem &downloadItem, bool overwrite) const
{
	const auto url = wingman::DownloadItemActions::urlForModel(downloadItem);
	const auto item = std::make_shared<wingman::DownloadItem>(wingman::DownloadItem{ downloadItem });
	auto request = wingman::curl::Request{ url };
	request.file.item = item;
	request.file.actions = actions.download();
	request.file.onProgress = onDownloadProgress;

	const auto response = wingman::curl::fetch(request);
}

void DownloadService::updateServerStatus(const wingman::DownloadServerAppItemStatus &status, std::optional<wingman::DownloadItem> downloadItem, std::optional<std::string> error)
{
	auto appItem = actions.app()->get(SERVER_NAME).value_or(wingman::AppItem::make(SERVER_NAME));

	nlohmann::json j = nlohmann::json::parse(appItem.value);
	auto downloadServerItem = j.get<wingman::DownloadServerAppItem>();
	downloadServerItem.status = status;
	if (error) {
		downloadServerItem.error = error;
	}
	if (downloadItem) {
		downloadServerItem.currentDownload.emplace(downloadItem.value());
	}
	if (onServiceStatus) {
		if (!onServiceStatus(&downloadServerItem)) {
			spdlog::debug(SERVER_NAME + ": (updateServerStatus) onServiceStatus returned false, stopping server.");
			stop();
		}
	}
	nlohmann::json j2 = downloadServerItem;
	appItem.value = j2.dump();
	actions.app()->set(appItem);
}

void DownloadService::runOrphanedDownloadCleanup() const
{
	// Check for orphaned downloads and clean up
	for (const auto downloads = actions.download()->getAll(); const auto & download : downloads) {
		if (download.status == wingman::DownloadItemStatus::complete) {
			// Check if the download file exists in the file system
			if (!actions.download()->fileExists(download)) {
				actions.download()->remove(download.modelRepo, download.filePath);
			}
		}
	}
	// Check for orphaned downloaded model files on disk and clean up
	for (const auto files = wingman::DownloadItemActions::getModelFiles(); const auto & file : files) {
		// get file names from disk and check if they are in the database
		if (const auto din = wingman::DownloadItemActions::parseDownloadItemNameFromSafeFilePath(file)) {
			const auto downloadItem = actions.download()->get(din.value().modelRepo, din.value().filePath);
			if (!downloadItem) {
				// get full path to file and remove it
				const auto fullPath = wingman::DownloadItemActions::getDownloadItemOutputPath(din.value().modelRepo, din.value().filePath);
				spdlog::info(SERVER_NAME + ": Removing orphaned file " + fullPath + " from disk.");
				std::filesystem::remove(fullPath);
			}
		}
	}
}

void DownloadService::initialize() const
{
	wingman::DownloadServerAppItem dsai;
	nlohmann::json j = dsai;
	wingman::AppItem item;
	item.name = SERVER_NAME;
	item.value = j.dump();
	actions.app()->set(item);

	runOrphanedDownloadCleanup();
	actions.download()->reset();
}

void DownloadService::run()
{
	try {
		if (!keepRunning) {
			return;
		}

		spdlog::debug(SERVER_NAME + "::run Download service started.");

		initialize();

		while (keepRunning) {
			updateServerStatus(wingman::DownloadServerAppItemStatus::ready);
			spdlog::trace(SERVER_NAME + "::run Checking for queued downloads...");
			if (auto nextItem = actions.download()->getNextQueued()) {
				auto &currentItem = nextItem.value();
				const std::string modelName = currentItem.modelRepo + ": " + currentItem.filePath;

				spdlog::info(SERVER_NAME + "::run Processing download of " + modelName + "...");

				if (currentItem.status == wingman::DownloadItemStatus::queued) {
					// Update status to downloading
					currentItem.status = wingman::DownloadItemStatus::downloading;
					actions.download()->set(currentItem);
					updateServerStatus(wingman::DownloadServerAppItemStatus::preparing, currentItem);

					spdlog::debug(SERVER_NAME + "::run calling startDownload " + modelName + "...");
					try {
						startDownload(currentItem, true);
					} catch (const std::exception &e) {
						spdlog::error(SERVER_NAME + "::run Exception (startDownload): " + std::string(e.what()));
						updateServerStatus(wingman::DownloadServerAppItemStatus::error, currentItem, e.what());
					}
					spdlog::info(SERVER_NAME + "::run Download of " + modelName + " complete.");
					updateServerStatus(wingman::DownloadServerAppItemStatus::ready);
				}
			}

			runOrphanedDownloadCleanup();

			spdlog::trace(SERVER_NAME + "::run Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
			std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
		}
		updateServerStatus(wingman::DownloadServerAppItemStatus::stopping);
		spdlog::debug(SERVER_NAME + "::run Download server stopped.");
	} catch (const std::exception &e) {
		spdlog::error(SERVER_NAME + "::run Exception (run): " + std::string(e.what()));
		stop();
	}
	updateServerStatus(wingman::DownloadServerAppItemStatus::stopped);
}

void DownloadService::stop()
{
	keepRunning = false;
}
