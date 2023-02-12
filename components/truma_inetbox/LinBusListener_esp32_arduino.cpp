#ifdef USE_ESP32_FRAMEWORK_ARDUINO
#include "LinBusListener.h"
#include "esphome/core/log.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include "soc/uart_reg.h"
#include "esphome/components/uart/truma_uart_component_esp32_arduino.h"
#include "esphome/components/uart/uart_component_esp32_arduino.h"

namespace esphome {
namespace truma_inetbox {

static const char *const TAG = "truma_inetbox.LinBusListener";

void LinBusListener::setup_framework() {
  auto uartComp = static_cast<esphome::uart::truma_ESP32ArduinoUARTComponent *>(this->parent_);

  auto uart_num = uartComp->get_hw_serial_number();
  auto hwSerial = uartComp->get_hw_serial();

  // Extract from `uartSetFastReading` - Can't call it because I don't have access to `uart_t` object.

  // Tweak the fifo settings so data is available as soon as the first byte is recieved.
  // If not it will wait either until fifo is filled or a certain time has passed.
  uart_intr_config_t uart_intr;
  uart_intr.intr_enable_mask =
      UART_RXFIFO_FULL_INT_ENA_M | UART_RXFIFO_TOUT_INT_ENA_M;  // only these IRQs - no BREAK, PARITY or OVERFLOW
  // UART_RXFIFO_FULL_INT_ENA_M | UART_RXFIFO_TOUT_INT_ENA_M | UART_FRM_ERR_INT_ENA_M |
  // UART_RXFIFO_OVF_INT_ENA_M | UART_BRK_DET_INT_ENA_M | UART_PARITY_ERR_INT_ENA_M;
  uart_intr.rxfifo_full_thresh =
      1;  // UART_FULL_THRESH_DEFAULT,  //120 default!! aghh! need receive 120 chars before we see them
  uart_intr.rx_timeout_thresh =
      10;  // UART_TOUT_THRESH_DEFAULT,  //10 works well for my short messages I need send/receive
  uart_intr.txfifo_empty_intr_thresh = 10;  // UART_EMPTY_THRESH_DEFAULT
  uart_intr_config(uart_num, &uart_intr);

  hwSerial->onReceive(
      [this]() {
        // Check if Lin Bus is faulty.
        if (this->fault_pin_ != nullptr) {
          if (!this->fault_pin_->digital_read()) {
            if (!this->fault_on_lin_bus_reported_) {
              this->fault_on_lin_bus_reported_ = true;
              ESP_LOGE(TAG, "Fault on LIN BUS detected.");
            }
            // Ignore any data present in buffer
            this->clear_uart_buffer_();
          } else if (this->fault_on_lin_bus_reported_) {
            this->fault_on_lin_bus_reported_ = false;
            ESP_LOGI(TAG, "Fault on LIN BUS fixed.");
          }
        }

        if (!this->fault_on_lin_bus_reported_) {
          while (this->available()) {
            // this->last_data_recieved_ = esp_timer_get_time();
            this->read_lin_frame_();
          }
        }
      },
      false);
  hwSerial->onReceiveError([this](hardwareSerial_error_t val) {
    if (val == UART_BREAK_ERROR) {
      // If the break is valid the `onReceive` is called first and the break is handeld. Therfore the expectation is
      // that the state should be in waiting for `SYNC`.
      if (this->current_state_ != READ_STATE_SYNC) {
        this->current_state_ = READ_STATE_BREAK;
      }
      return;
    } else if (val == UART_BUFFER_FULL_ERROR) {
      ESP_LOGW(TAG, "UART_BUFFER_FULL_ERROR");
    } else if (val == UART_FIFO_OVF_ERROR) {
      ESP_LOGW(TAG, "UART_FIFO_OVF_ERROR");
    } else if (val == UART_FRAME_ERROR) {
      ESP_LOGW(TAG, "UART_FRAME_ERROR");
    } else if (val == UART_PARITY_ERROR) {
      ESP_LOGW(TAG, "UART_PARITY_ERROR");
    }
  });
}

}  // namespace truma_inetbox
}  // namespace esphome

#endif  // USE_ESP32_FRAMEWORK_ARDUINO