#ifndef FIVE_STONES_MATCH_QUEUE_HPP
#define FIVE_STONES_MATCH_QUEUE_HPP

#include <condition_variable>
#include <list>
#include <mutex>

template <class T>
class match_queue
{
private:
    // 用链表而不直接使用 queue，是因为有中间删除数据的需要
    std::list<T> _list;
    std::mutex _mutex;               // 实现线程安全
    std::condition_variable _cond;   // 主要为了阻塞消费者，后边使用的时候：队列中元素个数<2则阻塞

public:
    // 获取队列元素个数
    int size()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return static_cast<int>(_list.size());
    }

    // 判断队列是否为空
    bool empty()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _list.empty();
    }

    // 阻塞等待（队列人数不足时供匹配线程等待）
    void wait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // 使用谓词避免“先 notify 后 wait”导致的丢唤醒
        _cond.wait(lock, [this]()
                   { return _list.size() >= 2; });
    }

    // 入队数据，并唤醒线程
    void push(const T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.push_back(data);
        _cond.notify_all(); // 唤醒所有等待的线程
    }

    // 出队数据
    bool pop(T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_list.empty())
            return false;     // 队列为空，返回false
        data = _list.front(); // 队列不为空，取出队列头元素
        _list.pop_front();
        return true; // 队列不为空，取出队列头元素成功，返回true
    }

    // 移除指定的数据
    void remove(const T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.remove(data); // 移除指定的数据
    }
};

#endif
