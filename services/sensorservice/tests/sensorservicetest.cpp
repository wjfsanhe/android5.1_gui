/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/sensor.h>
#include <gui/Sensor.h>
#include <gui/SensorManager.h>
#include <gui/SensorEventQueue.h>
#include <utils/Looper.h>

using namespace android;

static nsecs_t sStartTime = 0;


int receiver(int fd, int events, void* data)
{
    sp<SensorEventQueue> q((SensorEventQueue*)data);
    ssize_t n;
    ASensorEvent buffer[20];

    static nsecs_t oldTimeStamp[3] = {0};

    while ((n = q->read(buffer, 20)) > 0) {
        for (int i=0 ; i<n ; i++) {
            float t;

	    if (buffer[i].type == Sensor::TYPE_ACCELEROMETER) {
		    if (oldTimeStamp[0]) {
			    t = float(buffer[i].timestamp - oldTimeStamp[0]) / s2ns(1);
		    } else {
			    t = float(buffer[i].timestamp - sStartTime) / s2ns(1);
		    }
		    oldTimeStamp[0] = buffer[i].timestamp;
		    printf("AAAAA :[%f - %f]\t%8f\t%8f\t%8f\t%f\n",
				    t*1000,float(systemTime()-buffer[i].timestamp)/ s2ns(1)*1000,
				    buffer[i].data[0], buffer[i].data[1], buffer[i].data[2],
				    1.0/t);
	    }
	    
            if (buffer[i].type == Sensor::TYPE_GYROSCOPE) {
		
		    if (oldTimeStamp[1]) {
			    t = float(buffer[i].timestamp - oldTimeStamp[1]) / s2ns(1);
		    } else {
			    t = float(buffer[i].timestamp - sStartTime) / s2ns(1);
		    }
		    oldTimeStamp[1] = buffer[i].timestamp;
                printf("GGGGG :[%f - %f]\t%8f\t%8f\t%8f\t%f\n",
                        t*1000,float(systemTime()-buffer[i].timestamp)/ s2ns(1)*1000,
                        buffer[i].data[0], buffer[i].data[1], buffer[i].data[2],
                        1.0/t);
            }

            if (buffer[i].type == Sensor::TYPE_MAGNETIC_FIELD) {


		    if (oldTimeStamp[2]) {
			    t = float(buffer[i].timestamp - oldTimeStamp[2]) / s2ns(1);
		    } else {
			    t = float(buffer[i].timestamp - sStartTime) / s2ns(1);
		    }
		    oldTimeStamp[2] = buffer[i].timestamp;
                printf("MMMMM :[%f - %f]\t%8f\t%8f\t%8f\t%f\n",
                        t*1000,float(systemTime()-buffer[i].timestamp)/ s2ns(1)*1000,
                        buffer[i].data[0], buffer[i].data[1], buffer[i].data[2],
                        1.0/t);
            }


        }
    }
	float cur=systemTime();
	static float old=0.0;	
	printf("%f----%f\n\n",cur/s2ns(1),(cur-old)/s2ns(1)*1000.0);
	old=cur;


    if (n<0 && n != -EAGAIN) {
        printf("error reading events (%s)\n", strerror(-n));
    }
    return 1;
}


int main(int argc, char** argv)
{
    SensorManager& mgr(SensorManager::getInstance());

    Sensor const* const* list;
    ssize_t count = mgr.getSensorList(&list);
    printf("numSensors=%d\n", int(count));

    sp<SensorEventQueue> q = mgr.createEventQueue();
    printf("queue=%p\n", q.get());

    Sensor const* accelerometer = mgr.getDefaultSensor(Sensor::TYPE_ACCELEROMETER);
    Sensor const* gero = mgr.getDefaultSensor(Sensor::TYPE_GYROSCOPE);
    Sensor const* magic = mgr.getDefaultSensor(Sensor::TYPE_MAGNETIC_FIELD);
    printf("accelerometer=%p (%s)\n",
            accelerometer, accelerometer->getName().string());

    printf("accelerometer=%p (%s)\n",
           	gero, gero->getName().string());

    printf("magic=%p (%s)\n",
           	magic, magic->getName().string());

    sStartTime = systemTime();

    //q->enableSensor(accelerometer);
    q->enableSensor(gero);
    //q->enableSensor(magic);

    q->setEventRate(accelerometer, ms2ns(10));
    q->setEventRate(gero, ms2ns(10));
    q->setEventRate(magic, ms2ns(10));

    sp<Looper> loop = new Looper(false);
    loop->addFd(q->getFd(), 0, ALOOPER_EVENT_INPUT, receiver, q.get());

    do {
        //printf("about to poll...\n");
        int32_t ret = loop->pollOnce(-1);
        switch (ret) {
            case ALOOPER_POLL_WAKE:
                //("ALOOPER_POLL_WAKE\n");
                break;
            case ALOOPER_POLL_CALLBACK:
                //("ALOOPER_POLL_CALLBACK\n");
                break;
            case ALOOPER_POLL_TIMEOUT:
                printf("ALOOPER_POLL_TIMEOUT\n");
                break;
            case ALOOPER_POLL_ERROR:
                printf("ALOOPER_POLL_TIMEOUT\n");
                break;
            default:
                printf("ugh? poll returned %d\n", ret);
                break;
        }
    } while (1);


    return 0;
}
