#include "AndroidOut.h"
#include "Renderer.h"

#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

extern "C" {

#include <game-activity/native_app_glue/android_native_app_glue.c>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <android/log.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>

/*!
 * Handles commands sent to this Android application
 * @param pApp the app the commands are coming from
 * @param cmd the command to handle
 */
void handle_cmd(android_app *pApp, int32_t cmd) {
    aout << "Handling command: " << cmd << std::endl;
    
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            aout << "APP_CMD_INIT_WINDOW: Creating renderer" << std::endl;
            pApp->userData = new Renderer(pApp);
            if (!pApp->userData) {
                aout << "Failed to create renderer" << std::endl;
            } else {
                aout << "Renderer created successfully" << std::endl;
            }
            break;
            
        case APP_CMD_TERM_WINDOW:
            aout << "APP_CMD_TERM_WINDOW: Cleaning up" << std::endl;
            if (pApp->userData) {
                auto *pRenderer = reinterpret_cast<Renderer *>(pApp->userData);
                pApp->userData = nullptr;
                delete pRenderer;
                aout << "Renderer cleanup successful" << std::endl;
            }
            break;
            
        default:
            aout << "Unhandled command: " << cmd << std::endl;
            break;
    }
}

/*!
 * Enable the motion events you want to handle; not handled events are
 * passed back to OS for further processing. For this example case,
 * only pointer and joystick devices are enabled.
 *
 * @param motionEvent the newly arrived GameActivityMotionEvent.
 * @return true if the event is from a pointer or joystick device,
 *         false for all other input devices.
 */
bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent) {
    auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
    return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
            sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

/*!
 * This the main entry point for a native activity
 */
void android_main(struct android_app *pApp) {
    aout << "Starting android_main" << std::endl;

    pApp->onAppCmd = handle_cmd;
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    aout << "Entering main loop" << std::endl;
    
    // Set thread priority to highest
    int tid = syscall(__NR_gettid);  // Use syscall directly
    if (setpriority(PRIO_PROCESS, tid, -20) != 0) {  // Add error checking
        aout << "Warning: Could not set thread priority" << std::endl;
    }

    // Also request RT scheduling if available
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        // If SCHED_FIFO fails, try SCHED_RR
        if (sched_setscheduler(0, SCHED_RR, &param) != 0) {
            aout << "Warning: Could not set RT scheduling" << std::endl;
        }
    }
    
    do {
        bool done = false;
        while (!done) {
            int timeout = 0;
            int events;
            android_poll_source *pSource;
            
            int result = ALooper_pollOnce(timeout, nullptr, &events,
                                        reinterpret_cast<void**>(&pSource));
                                        
            switch (result) {
                case ALOOPER_POLL_TIMEOUT:
                case ALOOPER_POLL_WAKE:
                    done = true;
                    break;
                case ALOOPER_EVENT_ERROR:
                    aout << "Error in ALooper_pollOnce" << std::endl;
                    break;
                default:
                    if (pSource) {
                        pSource->process(pApp, pSource);
                    }
            }
        }

        if (pApp->userData) {
            auto *pRenderer = reinterpret_cast<Renderer *>(pApp->userData);
            pRenderer->handleInput();
            pRenderer->render();
        }
    } while (!pApp->destroyRequested);
    
    aout << "Main loop ended" << std::endl;
    aout << "Exiting android_main" << std::endl;
}
}