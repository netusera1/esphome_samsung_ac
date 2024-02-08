#include "esphome/core/log.h"
#include "protocol.h"
#include "util.h"
#include "protocol_nasa.h"
#include "protocol_non_nasa.h"

namespace esphome
{
    namespace samsung_ac
    {
        // This functions is designed to run after a new value was added
        // to the data vector. One by one.
        DecodeResult process_data(std::vector<uint8_t> &data, MessageTarget *target)
        {
            auto result = try_decode_non_nasa_packet(data);
            if (result.type == DecodeResultType::Processed)
            {
                process_non_nasa_packet(target);
                return result;
            }

            result = try_decode_nasa_packet(data);
            if (result.type == DecodeResultType::Processed)
            {
                process_nasa_packet(target);
            }
            return result;
        }

        bool is_nasa_address(const std::string &address)
        {
            return address.size() != 2;
        }

        AddressType get_address_type(const std::string &address)
        {
            if (address == "c8" || address.rfind("10.", 0) == 0)
                return AddressType::Outdoor;

            if (address == "00" || address == "01" || address == "02" || address == "03" || address.rfind("20.", 0) == 0)
                return AddressType::Indoor;

            return AddressType::Other;
        }

        Protocol *nasaProtocol = new NasaProtocol();
        Protocol *nonNasaProtocol = new NonNasaProtocol();

        Protocol *get_protocol(const std::string &address)
        {
            if (!is_nasa_address(address))
                return nonNasaProtocol;

            return nasaProtocol;
        }
    } // namespace samsung_ac
} // namespace esphome