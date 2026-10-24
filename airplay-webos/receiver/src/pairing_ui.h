#pragma once
#include <string>

class PairingOverlayState {
public:
    bool update_pin(const std::string& pin) { return set_pin(pin.substr(0,8)); }
    bool completed() { return set_pin(std::string()); }
    bool cancelled() { return set_pin(std::string()); }
    bool reset() { return set_pin(std::string()); }
    const std::string& pin() const { return pin_; }
    bool visible() const { return !pin_.empty(); }
private:
    bool set_pin(const std::string& pin) {
        if(pin_==pin)return false;
        pin_=pin;
        return true;
    }
    std::string pin_;
};

enum class UiModal { None, Donation, Pairing };
enum class PairingInputResult { PassThrough, Consumed, Cancel };
inline PairingInputResult pairing_input_result(bool pairing_pending, bool is_back, bool is_exit) {
    if(!pairing_pending)return PairingInputResult::PassThrough;
    if(is_exit)return PairingInputResult::PassThrough;
    if(is_back)return PairingInputResult::Cancel;
    return PairingInputResult::Consumed;
}
inline UiModal active_ui_modal(bool pairing_pending, bool donation_open) {
    if(pairing_pending)return UiModal::Pairing;
    if(donation_open)return UiModal::Donation;
    return UiModal::None;
}
