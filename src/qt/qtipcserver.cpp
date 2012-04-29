// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/tokenizer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "headers.h"

using namespace boost::interprocess;
using namespace boost::posix_time;
using namespace boost;
using namespace std;

void ipcShutdown()
{
    message_queue::remove("BitcoinURL");
}

void ipcThread(void* parg)
{
    message_queue* mq = (message_queue*)parg;
    char strBuf[257];
    size_t nSize;
    unsigned int nPriority;
    loop
    {
        ptime d = boost::posix_time::microsec_clock::universal_time() + millisec(100);
        if(mq->timed_receive(&strBuf, sizeof(strBuf), nSize, nPriority, d))
        {
            ThreadSafeHandleURL(std::string(strBuf, nSize));
            Sleep(1000);
        }
        if (fShutdown)
        {
            ipcShutdown();
            break;
        }
    }
    ipcShutdown();
}

void ipcInit()
{
#ifdef MAC_OSX
    // TODO: implement litecoin: URI handling the Mac Way
    return;
#endif

    message_queue* mq;
    char strBuf[257];
    size_t nSize;
    unsigned int nPriority;
    try {
        mq = new message_queue(open_or_create, "BitcoinURL", 2, 256);

        // Make sure we don't lose any litecoin: URIs
        for (int i = 0; i < 2; i++)
        {
            ptime d = boost::posix_time::microsec_clock::universal_time() + millisec(1);
            if(mq->timed_receive(&strBuf, sizeof(strBuf), nSize, nPriority, d))
            {
                ThreadSafeHandleURL(std::string(strBuf, nSize));
            }
            else
                break;
        }

        // Make sure only one bitcoin instance is listening
        message_queue::remove("BitcoinURL");
        mq = new message_queue(open_or_create, "BitcoinURL", 2, 256);
    }
    catch (interprocess_exception &ex) {
        return;
    }
    if (!CreateThread(ipcThread, mq))
    {
        delete mq;
    }
}
