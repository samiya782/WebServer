#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <condition_variable>
#include <future>
#include <semaphore.h>
#include <thread>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;                //线程池中的线程数
    int m_max_requests;                 //请求队列中允许的最大请求数
    SafeQueue<T *> m_workqueue;         //执行函数安全队列，即请求队列
    std::vector<std::thread> m_threads; //描述线程池的数组
    sem m_queuestat;                    //是否有任务需要处理
    bool m_stop;                        //是否结束线程
    connection_pool *m_connPool;        //数据库
};
template <typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(thread_number), m_connPool(connPool), m_queuestat(0)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        //printf("create the %dth thread\n",i);
        m_threads.at(i) = std::thread(ThreadWorker(worker, this));
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    m_stop = true;
    for (int i = 0; i < m_threads.size(); ++i)
    {
        if (m_threads.at(i).joinable()) // 判断线程是否在等待
        {
            m_threads.at(i).join(); // 将线程加入到等待队列
        }
    }
}
template <typename T>
bool threadpool<T>::append(T *request)
{
    if (m_workqueue.size() > m_max_requests)
    {
        return false;
    }
    m_workqueue.enqueue(request);
    m_queuestat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        if (m_workqueue.empty())
        {
            continue;
        }
        T *request;
        m_workqueue.dequeue(request);
        if (!request)
            continue;

        connectionRAII mysqlcon(&request->mysql, m_connPool);
        
        request->process();
    }
}