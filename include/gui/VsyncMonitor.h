#ifndef VSYNC_MONITOR_H_
#define VSYNC_MONITOR_H_

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/Timers.h>

#include <gui/BufferQueueDefs.h>
#include <gui/DisplayEventReceiver.h>
#include <utils/Log.h>
#include <utils/Looper.h>
#include <utils/threads.h>
#include <gui/DisplayEventReceiver.h>

namespace android {
// ---------------------------------------------------------------------------

class  VsyncMonitor :public LooperCallback ,public Thread{
//
public:
	VsyncMonitor(const sp<BufferQueueCoreBF>& core);
	status_t Init();
	status_t requestNextVsync();
	virtual int handleEvent(int receiveFd, int events, void* data);

protected:
	~VsyncMonitor();


private:
	bool  mInitComplete;
	virtual bool        threadLoop();
	virtual void        onFirstRef();

	bool processPendingEvents(nsecs_t* outTimestamp, 
	int32_t* outId, uint32_t* outCount);
	void dispatchVsync(nsecs_t timestamp, int32_t id, uint32_t count);

//we should hold one looper.
	sp<Looper> mLooper;
	DisplayEventReceiver mReceiver;
	static const size_t EVENT_BUFFER_SIZE = 100;
	sp<BufferQueueCoreBF> mCore;

};

// ---------------------------------------------------------------------------

}; // namespace android

#endif  
