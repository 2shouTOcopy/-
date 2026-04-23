#ifndef FTP_LOG_MANAGER_H
#define FTP_LOG_MANAGER_H

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <set>
#include <thread>

#define LOG_RECORD_DIR    "/mnt/log/app/"
#define LOG_RECORD_INDEX  "index_file.txt"
#define LOG_RECORD_ZIP    "log_record_file.zip"
#define LOG_RECORD_INDEX_PATH  LOG_RECORD_DIR LOG_RECORD_INDEX
#define LOG_RECORD_ZIP_PATH    LOG_RECORD_DIR LOG_RECORD_ZIP
#define LOG_RECORD_UNZIP_PATH  "/mnt/data/log_tmp/"

class FtpLogManager
{
public:
	FtpLogManager();

	~FtpLogManager();

	FtpLogManager(const FtpLogManager &) = delete;

	FtpLogManager &operator=(const FtpLogManager &) = delete;

	static FtpLogManager &getInstance();

	bool enable(FtpClientManager* pClientInstance, int nLogId);

	void disable(FtpClientManager* pClientInstance);
	
	void triggerLogTransferWithDelay();

	void cancelDelayedTransfer();

private:
	void start();

	void stop();

	void processLogTransfer();

	void triggerLogTransfer();

	FtpClientManager *m_pMessageObj;

	FtpClientUtils m_utils;

	int m_nLogId;
	std::queue<std::string> m_logQueue;      // Queue to store log files to be transferred
	std::set<std::string> m_transferredLogs; // Set to keep track of transferred logs
	std::mutex m_mutex;                      // Mutex to protect access to the log queue and transferred logs set
	std::condition_variable m_condition;     // Condition variable to signal log transfer
	std::condition_variable m_delayCondition; // Separate condition for delay thread
	std::thread m_thread;                    // Thread to process log transfers
	std::thread m_delayThread;               // Thread to handle delay before triggering log transfer
	std::atomic<bool> m_waitingForTrigger;   // Atomic flag to indicate if a delayed trigger is scheduled
	std::atomic<bool> m_stop;                // Flag to stop the log transfer thread
	std::atomic<bool> m_active;              // Flag to indicate if the thread has been started
	bool m_cancelDelay;                      // Flag to cancel the delayed trigger
};

#endif
