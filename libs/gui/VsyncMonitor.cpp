#include <utils/threads.h>
#include <cutils/iosched_policy.h>
#include <utils/Errors.h>
#include <gui/BufferQueueCoreBF.h>
#include  <gui/VsyncMonitor.h>
#include <inttypes.h>
#define LOG_TAG "VsyncMonitor"


namespace android {

// ---------------------------------------------------------------------------
VsyncMonitor::VsyncMonitor(const sp<BufferQueueCoreBF>& core):
	mCore(core),
	mInitComplete(false)
{
	ALOGD("VsyncMonitor constructor be called");	
}

void VsyncMonitor::onFirstRef() {
    run("VsyncMonitor", PRIORITY_URGENT_DISPLAY + PRIORITY_MORE_FAVORABLE);
    android_set_rt_ioprio(getTid(), 1);
}
status_t VsyncMonitor::Init() {
	status_t result = mReceiver.initCheck();
	if (result) {
		ALOGE("Failed to initialize display event receiver, status=%d", result);
		return result;
	}
	mLooper = new Looper(false);
	/*if(Looper::getForThread() != NULL){
		ALOGD("WOW  loooper is not null");
	}*/
	Looper::setForThread(mLooper);
	ALOGD("initCheck ok..");
	if(mLooper != NULL ){
		int32_t rc;
		rc = mLooper->addFd(mReceiver.getFd(), 0, Looper::EVENT_INPUT,
            	this,(void*)this);
		if(rc < 0){
			ALOGD("addFd failed");
			return INVALID_OPERATION;
		} else {
			ALOGD("addFd and init success");
			mReceiver.setVsyncRate(1);
			return NO_ERROR ;
		}
	} else {
		ALOGE("Vsync monitor init fail");
		return BAD_VALUE ;
	}
}

VsyncMonitor::~VsyncMonitor() {

	ALOGE("VsyncMonitor destructor~");

}

void VsyncMonitor::dispatchVsync(nsecs_t timestamp, int32_t id, uint32_t count){
	ALOGD("receiver %p ~ Invoking vsync handler.", this);	
	ALOGD( " time=%" PRIu64" display[%d] count[%d]\n, Enable mcore dequeue",
			timestamp,id,count);
 	
	switch (mCore->mCurBufferIdx){
		case 0x11:
		ALOGD("first Vsync triggered");
		mCore->mCurBufferIdx=0;
		break;
		case 0x00:
		case 0x01:
		mCore->mCurBufferIdx^=0x1 ;
		ALOGD("common toggle");
		break;
	}
		
	//enable or reenable enqueue buffer.
	mCore->mBufferEnqueueEnable=true;
	
	return ;

}
//main process function.
int VsyncMonitor::handleEvent(int receiveFd, int events, void* data) {
    ALOGD("HandleEvent be called*************");
    if (events & (Looper::EVENT_ERROR | Looper::EVENT_HANGUP)) {
        ALOGE("Display event receiver pipe was closed or an error occurred.  "
                "events=0x%x", events);
        return 0; // remove the callback
    }

    if (!(events & Looper::EVENT_INPUT)) {
        ALOGD("Received  unhandled poll event.  "
                "events=0x%x", events);
        return 1; // keep the callback
    }

    // Drain all pending events, keep the last vsync.
    nsecs_t vsyncTimestamp;
    int32_t vsyncDisplayId;
    uint32_t vsyncCount;
    if (processPendingEvents(&vsyncTimestamp, &vsyncDisplayId, &vsyncCount)) {
        ALOGD("receiver %p ~ Vsync pulse: timestamp=%lld, id=%d, count=%d",
                this, vsyncTimestamp, vsyncDisplayId, vsyncCount);
        dispatchVsync(vsyncTimestamp, vsyncDisplayId, vsyncCount);
    }

    return 1; // keep the callback
}
bool VsyncMonitor::processPendingEvents(
        nsecs_t* outTimestamp, int32_t* outId, uint32_t* outCount) {
    bool gotVsync = false;
    DisplayEventReceiver::Event buf[EVENT_BUFFER_SIZE];
    ssize_t n;
    while ((n = mReceiver.getEvents(buf, EVENT_BUFFER_SIZE)) > 0) {
        ALOGD("receiver %p ~ Read %d events.", this, int(n));
        for (ssize_t i = 0; i < n; i++) {
            const DisplayEventReceiver::Event& ev = buf[i];
            switch (ev.header.type) {
            case DisplayEventReceiver::DISPLAY_EVENT_VSYNC:
                // Later vsync events will just overwrite the info from earlier
                // ones. That's fine, we only care about the most recent.
                gotVsync = true;
                *outTimestamp = ev.header.timestamp;
                *outId = ev.header.id;
                *outCount = ev.vsync.count;
                break;
            case DisplayEventReceiver::DISPLAY_EVENT_HOTPLUG:
		ALOGD("receive hotplug event");
                break;
            default:
                ALOGD("receiver %p ~ ignoring unknown event type %#x", this, ev.header.type);
                break;
            }
        }
    }
    if (n < 0) {
        ALOGD("Failed to get events from display event receiver, status=%d", status_t(n));
    }
    return gotVsync;
}


status_t VsyncMonitor::requestNextVsync(){
	status_t status = mReceiver.requestNextVsync();
	if (status) {
		ALOGD("Failed to request next vsync, status=%d", status);
		return status;
	}	
	ALOGD("request Next Vsync OK");
	return OK;
}
bool	VsyncMonitor::threadLoop(){
	if(!mInitComplete){
		Init();
		mInitComplete=true;
	}
	usleep(20000);	
	requestNextVsync();
	return true;	
}
// ---------------------------------------------------------------------------

}; // namespace android

