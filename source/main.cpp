#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <stdarg.h>
#include <string>
#include <vector>

#include "ntp-client.hpp"
#include "servers.hpp"
#include "tesla-ext.hpp"

TimeServiceType __nx_time_service_type = TimeServiceType_System;

class NtpGui : public tsl::Gui {
private:
    std::string Message;
    int currentServer = 0;
    bool blockFlag = false;
    std::vector<std::string> serverAddresses;

    char* getCurrentServer() {
        return &serverAddresses[currentServer][0];
    }

    void setMessage(const char* fmt = "", ...) {
        char buff[200];
        va_list va;
        va_start(va, fmt);
        vsnprintf(buff, sizeof(buff), fmt, va);
        va_end(va);

        Message = buff;
    }

    bool setNetworkSystemClock(time_t time) {
        Result rs = timeSetCurrentTime(TimeType_NetworkSystemClock, (uint64_t)time);
        if (R_FAILED(rs)) {
            return false;
        }
        return true;
    }

    void setTime() {
        char* srv = getCurrentServer();
        NTPClient* client = new NTPClient(srv);
        time_t ntpTime = client->getTime();

        if (!client->hasFailed()) {
            if (setNetworkSystemClock(ntpTime)) {
                setMessage("Synced with %s", srv);
            } else {
                setMessage("Unable to set network clock.");
            }
        } else {
            setMessage("Error: %s", client->getErrorMessage().c_str());
        }

        delete client;
    }

    void setNetworkTimeAsUser() {
        time_t userTime, netTime;

        Result rs = timeGetCurrentTime(TimeType_UserSystemClock, (u64*)&userTime);
        if (R_FAILED(rs)) {
            setMessage("GetTimeUser %x", rs);
            return;
        }

        std::string usr = "User time!";
        std::string gr8 = "";
        rs = timeGetCurrentTime(TimeType_NetworkSystemClock, (u64*)&netTime);
        if (!R_FAILED(rs) && netTime < userTime) {
            gr8 = " Great Scott!";
        }

        if (setNetworkSystemClock(userTime)) {
            setMessage(usr.append(gr8).c_str());
        } else {
            setMessage("Unable to set network clock.");
        }
    }

    void getOffset() {
        time_t currentTime;
        Result rs = timeGetCurrentTime(TimeType_NetworkSystemClock, (u64*)&currentTime);
        if (R_FAILED(rs)) {
            setMessage("GetTimeNetwork %x", rs);
            return;
        }

        char* srv = getCurrentServer();
        NTPClient* client = new NTPClient(srv);
        time_t ntpTimeOffset = client->getTimeOffset(currentTime);

        if (!client->hasFailed()) {
            setMessage("Offset: %+ds", ntpTimeOffset);
        } else {
            setMessage("Error: %s", client->getErrorMessage().c_str());
        }

        delete client;
    }

    void serverChanged() {
        setMessage();
    }

    bool operationBlock(std::function<void()> f) {
        if (!blockFlag) {
            blockFlag = true;
            setMessage();
            f(); // TODO: Call async and set blockFlag to false
            blockFlag = false;
        }
        return !blockFlag;
    }

    std::function<bool(u64 keys)> syncListener = [this](u64 keys) {
        if (keys & HidNpadButton_A) {
            return operationBlock([&]() {
                setTime();
            });
        }
        return false;
    };

    std::function<bool(u64 keys)> offsetListener = [this](u64 keys) {
        if (keys & HidNpadButton_Y) {
            return operationBlock([&]() {
                getOffset();
            });
        }
        return false;
    };

public:
    NtpGui() : serverAddresses(NTPSERVERS[1].begin(), NTPSERVERS[1].end()) {
        setMessage();
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::CustomOverlayFrame("QuickNTP", "by NedEX - v1.2.0");

        auto list = new tsl::elm::List();

        list->setClickListener([this](u64 keys) {
            if (keys & (HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                setMessage();
                return true;
            }
            return false;
        });

        list->addItem(new tsl::elm::CategoryHeader("Pick server   |   \uE0E0  Sync   |   \uE0E3  Offset"));

        auto* trackbar = new tsl::elm::NamedStepTrackBar("\uE017", NTPSERVERS[0]);
        trackbar->setValueChangedListener([this](u8 val) {
            currentServer = val;
            serverChanged();
        });
        trackbar->setClickListener([this](u8 val) {
            return syncListener(val) || offsetListener(val);
        });
        list->addItem(trackbar);

        auto* syncTimeItem = new tsl::elm::ListItem("Sync time");
        syncTimeItem->setClickListener(syncListener);
        list->addItem(syncTimeItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("Syncs the time with the selected server.", false, x + 20, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      50);

        auto* getOffsetItem = new tsl::elm::ListItem("Get offset");
        getOffsetItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                return operationBlock([&]() {
                    getOffset();
                });
            }
            return false;
        });
        list->addItem(getOffsetItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("Gets the seconds offset with the selected server.\n\n\uE016  A value of ± 3 seconds is acceptable.", false, x + 20, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      70);

        auto* setToInternalItem = new tsl::elm::ListItem("User-set time");
        setToInternalItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                return operationBlock([&]() {
                    setNetworkTimeAsUser();
                });
            }
            return false;
        });
        list->addItem(setToInternalItem);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
                          renderer->drawString("Sets the network time to the user-set time.", false, x + 20, y + 20, 15, renderer->a(tsl::style::color::ColorDescription));
                      }),
                      50);

        list->addItem(new tsl::elm::CustomDrawerUnscissored([&message = Message](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
            if (!message.empty()) {
                renderer->drawString(message.c_str(), false, x + 5, tsl::cfg::FramebufferHeight - 100, 20, renderer->a(tsl::style::color::ColorText));
            }
        }));

        frame->setContent(list);
        return frame;
    }
};

class NtpOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        ASSERT_FATAL(socketInitializeDefault());
        ASSERT_FATAL(nifmInitialize(NifmServiceType_User));
        ASSERT_FATAL(timeInitialize());
        ASSERT_FATAL(smInitialize());
    }

    virtual void exitServices() override {
        socketExit();
        nifmExit();
        timeExit();
        smExit();
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<NtpGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<NtpOverlay>(argc, argv);
}
