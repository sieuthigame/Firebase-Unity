//
//  JniEventProcessor.cpp
//  XCodePlugin
//
//  Created by benwu on 9/1/15.
//
//

#include "JniEventProcessor.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <dispatch/dispatch.h>
#include "JniDataSnapshot.h"
#include "jnistub_ChildEventListenerStub.h"
#include "jnistub_ValueEventListenerStub.h"
#include "plugin.pch"
#include "JniHelper.h"

JniEventProcessor* JniEventProcessor::s_this = NULL;
std::mutex JniEventProcessor::s_lock;

JniEventProcessor::JniEventProcessor()
{
    m_currentStart = 0;
    m_currentEnd = 0;
    m_cFull = dispatch_semaphore_create(BUFFER_SIZE);
    m_cEmpty = dispatch_semaphore_create(0);
    
    m_isTerminated = true;
}

void JniEventProcessor::EnsureThread() {
    lock<std::mutex> lock(m_threadHold);
    if (m_isTerminated) {
        m_isTerminated = false;
        pthread_create( &m_tid, NULL, JniEventProcessor::ThreadStart, this);
    }
}

void JniEventProcessor::TerminateThread() {
    lock<std::mutex> lock(m_threadHold);
    if (!m_isTerminated) {
        m_isTerminated = true;
        dispatch_semaphore_signal(m_cEmpty); /* post to empty */
    }
}

JniEventProcessor* JniEventProcessor::GetInstance() {
    if (s_this == NULL) {
        lock<std::mutex> lock(s_lock);
        if (s_this == NULL) {
            s_this = new JniEventProcessor();
        }
    }
    s_this->EnsureThread();
    return s_this;
}

void* JniEventProcessor::ThreadStart(void* thisPtr) {
    JniEventProcessor* pThis = (JniEventProcessor*)thisPtr;
    pThis->ProcessLoop();
    return NULL;
}

void JniEventProcessor::EnqueueEvent(JniEvent* item)
{
    dispatch_semaphore_wait(m_cFull, DISPATCH_TIME_FOREVER); /* Wait if full */
    lock<std::mutex> lock(m_mutex);
    enQ(item);
    dispatch_semaphore_signal(m_cEmpty); /* post to empty */
}

void JniEventProcessor::ProcessLoop()
{
    while(true)
    {
        dispatch_semaphore_wait(m_cEmpty, DISPATCH_TIME_FOREVER); /* Wait if full */
        lock<std::mutex> lock(m_mutex);
        if (m_isTerminated) {
            break;
        }

        //consume
        JniEvent* pEvent = deQ();
        if (pEvent == NULL) {
            break;
        }
        try {
            pEvent->Process();
            delete pEvent;
        }
        catch(...) {
            //TODO log an error thru debuglog
            //we do our best not to let this thread die or else you dont get firebase events.
        }
        dispatch_semaphore_signal(m_cFull); /* post to full */
    }
}

void JniEventProcessor::enQ(JniEvent* item)
{
    m_buffer[m_currentEnd] = item;
    m_currentEnd++;
    m_currentEnd = m_currentEnd % BUFFER_SIZE;
}

JniEvent* JniEventProcessor::deQ()
{
    JniEvent* result = m_buffer[m_currentStart];
    m_currentStart++;
    m_currentStart = m_currentStart % BUFFER_SIZE;
    return result;
}
