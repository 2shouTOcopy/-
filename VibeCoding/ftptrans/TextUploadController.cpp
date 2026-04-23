#include "TextUploadController.h"

TextUploadController::TextUploadController() : m_refresh_step(1), m_pending_count(0) {}

void TextUploadController::setRefreshStep(size_t refresh_step) {
  m_refresh_step = refresh_step;
  if (m_refresh_step == 0) {
    m_refresh_step = 1;
  }
}

void TextUploadController::reset() { m_pending_count = 0; }

bool TextUploadController::onRecordAccepted() {
  ++m_pending_count;
  if (m_pending_count < m_refresh_step) {
    return false;
  }

  m_pending_count = 0;
  return true;
}

bool TextUploadController::onStopRequested() {
  if (m_pending_count == 0) {
    return false;
  }

  m_pending_count = 0;
  return true;
}
