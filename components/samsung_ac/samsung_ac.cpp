#include "samsung_ac.h"
#include "debug_mqtt.h"
#include "util.h"
#include "log.h"
#include <algorithm>
#include <vector>
#include <thread>

namespace esphome
{
  namespace samsung_ac
  {
    void Samsung_AC::setup()
    {
    }

    void Samsung_AC::update()
    {
      debug_mqtt_connect(debug_mqtt_host, debug_mqtt_port, debug_mqtt_username, debug_mqtt_password);

      // Waiting for first update before beginning processing data
      if (data_processing_init)
      {
        LOGC("Data Processing starting");
        data_processing_init = false;
      }

      std::string devices = "";
      for (const auto &pair : devices_)
      {
        devices += devices.length() > 0 ? ", " + pair.second->address : pair.second->address;
      }
      LOGC("Configured devices: %s", devices.c_str());

      std::string knownIndoor = "";
      std::string knownOutdoor = "";
      std::string knownOther = "";
      for (auto const &address : addresses_)
      {
        switch (get_address_type(address))
        {
        case AddressType::Outdoor:
          knownOutdoor += knownOutdoor.length() > 0 ? ", " + address : address;
          break;
        case AddressType::Indoor:
          knownIndoor += knownIndoor.length() > 0 ? ", " + address : address;
          break;
        default:
          knownOther += knownOther.length() > 0 ? ", " + address : address;
          break;
        }
      }
      LOGC("Discovered devices:");
      LOGC("  Outdoor: %s", (knownOutdoor.length() == 0 ? "-" : knownOutdoor.c_str()));
      LOGC("  Indoor:  %s", (knownIndoor.length() == 0 ? "-" : knownIndoor.c_str()));
      if (knownOther.length() > 0)
        LOGC("  Other:   %s", knownOther.c_str());
    }

    void Samsung_AC::register_device(Samsung_AC_Device *device)
    {
      if (find_device(device->address) != nullptr)
      {
        LOGW("There is already and device for address %s registered.", device->address.c_str());
        return;
      }

      devices_.insert({device->address, device});
    }

    void Samsung_AC::dump_config()
    {
    }

    void Samsung_AC::publish_data(uint8_t id, std::vector<uint8_t> &data, DataCallback *callback)
    {
      const uint32_t now = millis();

      OutgoingData outData;
      outData.data = data;
      outData.id = id;
      outData.nextRetry = 0;
      outData.retries = 0;
      outData.timeout = now + sendTimeout;
      outData.callback = callback;
      send_queue_.push_back(outData);
    }

    void Samsung_AC::ack_data(uint8_t id)
    {
        if (send_queue_.size() > 0)
        {
          auto senddata = send_queue_.front();
          if (senddata.id == id) {
            send_queue_.pop_front();
            senddata.callback->data_sent(id);
          }
        }
    }

    void Samsung_AC::loop()
    {
      if (data_processing_init)
        return;

      // if more data is expected, do not allow anything to be written
      if (!read_data())
        return;

      // If there is no data we use the time to send
      write_data();
    }

    uint16_t Samsung_AC::skip_data()
    {
      // Skip over filler data or broken packets
      // Example:
      // 320037d8fedbff81cb7ffbfd808d00803f008243000082350000805e008031008248ffff801a0082d400000b6a34 f9f6f1f9f9 32000e200002
      // Note that first part is a mangled packet, then regular filler data, then start of a new packet
      // and that one new proper packet will continue with the next data read

      // find next value of 0x32, and retry with that one
      for (uint16_t i = 1; i < data_.size(); i++)
      {
        if (data_[i] == 0x32)
        {
          return i;
        }
      }
      return data_.size();
    }

    bool Samsung_AC::read_data()
    {
      // read as long as there is anything to read
      while (available())
      {
        uint8_t c;
        if (read_byte(&c))
        {
          data_.push_back(c);
        }
      }

      if (data_.size() == 0)
        return true;

      const uint32_t now = millis();

      auto result = process_data(data_, this);
      if (result.type == DecodeResultType::Fill)
        return false;

      uint16_t skipBytes = data_.size();
      if (result.type == DecodeResultType::Processed)
        // number of bytes processed by protocol from the buffer
        skipBytes = result.bytes;
      else if (result.type == DecodeResultType::Skip)
        // find next potential packet start byte
        skipBytes = skip_data();

      if(result.type == DecodeResultType::Skip)
        LOG_RAW_DISCARDED(">>",  now-last_transmission_, data_, 0, skipBytes);
      else 
        LOG_RAW(">>",  now-last_transmission_, data_, 0, skipBytes);

      if (skipBytes == data_.size())
        data_.clear();
      else
      {
        std::move(begin(data_) + skipBytes, end(data_), begin(data_));
        data_.resize(data_.size() - skipBytes);
      }
      last_transmission_ = now;

      return false;
    }

    void Samsung_AC::write_data()
    {
      if (send_queue_.size() == 0)
        return;

      auto senddata = send_queue_.front();

      const uint32_t now = millis();
      if (senddata.timeout <= now && senddata.retries >= minRetries) {
        LOGE("Packet sending timeout %d after %d retries", senddata.id, senddata.retries);
        send_queue_.pop_front();
        senddata.callback->data_timeout(senddata.id);
        return;
      }

      if (now-last_transmission_ > silenceInterval && senddata.nextRetry < now)
      {
        if (senddata.nextRetry > 0){
          LOGW("Retry sending packet %d", senddata.id);
          senddata.retries++;
        }

        LOG_RAW("<<",  now-last_transmission_,  senddata.data);

        last_transmission_ = now;
        senddata.nextRetry = now + retryInterval;

        this->write_array(senddata.data);
        this->flush();

        send_queue_.pop_front();
        if (senddata.id > 0) {
          send_queue_.push_front(senddata);
        }
      }
    }

    float Samsung_AC::get_setup_priority() const { return setup_priority::DATA; }
  } // namespace samsung_ac
} // namespace esphome
