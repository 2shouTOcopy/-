#ifndef TEXT_UPLOAD_CONTROLLER_H
#define TEXT_UPLOAD_CONTROLLER_H

#include <stddef.h>

class TextUploadController {
 public:
  TextUploadController();

  void setRefreshStep(size_t refresh_step);
  void reset();
  bool onRecordAccepted();
  bool onStopRequested();

 private:
  size_t m_refresh_step;
  size_t m_pending_count;
};

#endif
