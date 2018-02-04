#ifndef __MLINKEDBLOCKINGQUEUE_H__
#define __MLINKEDBLOCKINGQUEUE_H__

/////////////////////////////////////////////////////////////////////////
// monolith: 地形管理程序
// CMLinkedBlockingQueue: 基于链表实现的阻塞队列
#include <atomic>
#include <limits.h>
#include <mutex>
#include <condition_variable>

template <typename E>
class CMLinkedBlockingQueue
{
	class CNode
	{
	public:
		CNode() : _pItem(nullptr), _pNext(nullptr) {}
		explicit CNode(E* x) : _pNext(nullptr), _pItem(x) {}
		CNode(const CNode& pNode) : _pNext(pNode._pNext), _pItem(pNode._pItem){}
		~CNode()
		{
			if (_pItem != nullptr)
			{
				delete _pItem;
				_pItem = nullptr;
				_pNext = nullptr;
			}
		}

		E*			_pItem;	// new
		CNode*		_pNext;	// reference
	};

public:
	CMLinkedBlockingQueue() :CMLinkedBlockingQueue(INT_MAX) {}
	CMLinkedBlockingQueue(unsigned int capacity)
		:m_nCapacity(capacity)
	{
		m_pHead = new CNode;	// 元素为空的头指针
		m_pTail = m_pHead;		// 初始时，尾节点就是头结点
		m_nCount.store(0);		// 初始元素数目为0
	}
	~CMLinkedBlockingQueue()
	{
		// 1.清空元素
		Clear();
		// 2.删除头结点内存
		delete m_pHead;
		m_pTail = m_pHead = nullptr;
	}

public:
	// 获取元素数目
	inline unsigned int Size() const { return m_nCount.load(); }
	// 获取剩余容量
	inline unsigned int GetRemainCapacity() const { return m_nCapacity - m_nCount.load(); }
	// 插入元素到队列尾部
	void Put(E* x);
	// 插入元素到队列尾部，如果队列满了，则等待给定的毫秒时间
	// 等待给定时间后还满，则返回false
	bool Offer(E* x, long timeout);
	// 插入元素到队列尾部，如果队列满了，则返回false
	bool Offer(E* x);
	// 从队列首部取元素
	E* Take();
	// 从队列首部取元素，如果队列为空，则等待给定毫秒数
	// 等待时间到达后还为空，则返回null
	E* Poll(long timeout);
	// 从队列首部取元素，如果队列为空，则返回null
	E* Poll();
	// 得到第一个元素
	E* Peek();
	// 移除某个元素
	bool Remove(E* x);
	// 清空元素
	void Clear();
protected:
	// 取消某个节点的链接
	void unlink(CNode* p, CNode* trail);

	// 对并发锁完全加锁
	inline void fullyLock()
	{
		m_hPutLock.lock();
		m_hTakeLock.lock();
	}

	// 对并发锁完全解锁
	inline void fullyUnlock()
	{
		m_hTakeLock.unlock();
		m_hPutLock.unlock();
	}
private:
	// 触发take/offer等待信号量
	inline void signalNotEmpty()
	{
		std::unique_lock<std::recursive_timed_mutex> lock(m_hTakeLock);
		m_hNotEmptyCondition.notify_one();
	}

	// 触发put/poll等待信号量
	inline void signalNotFull()
	{
		std::unique_lock<std::recursive_timed_mutex> lock(m_hPutLock);
		m_hNotFullCondition.notify_one();
	}

	// 创建一个节点，连接到队列尾部
	void enqueue(E* x);
	// 从头部移除一个节点，返回移除节点的元素
	E* dequeue();

private:
	// 容量上限	// 默认最大int
	unsigned int		m_nCapacity;
	// 队列元素数目 原子操作
	std::atomic_uint	m_nCount;
	// 队列元素标识
	// 初始时，头指针和尾指针都指向同一个值为null的节点
	CNode*				m_pHead;	// 头节点	哨子节点
	CNode*				m_pTail;	// 尾节点	next指针指向自身
	// 并发锁
	std::recursive_timed_mutex	m_hTakeLock;	// take,poll等操作持有的锁
	std::recursive_timed_mutex	m_hPutLock;		// put,offer等操作持有的锁
	std::condition_variable_any	m_hNotEmptyCondition;	// take操作需要等待的条件
	std::condition_variable_any	m_hNotFullCondition;	// put操作需要等待的条件
};


template <typename E>
void CMLinkedBlockingQueue<E>::unlink(CNode* p, CNode* trail)
{
	trail->_pNext = p->_pNext;
	if (m_pTail == p)
	{
		if (m_pHead->_pNext == p)	// 唯一的一个节点
		{
			// 保证头节点是元素为空的节点
			delete p->_pItem;	// 删除元素内存
			p->_pItem = nullptr;
			// 此时元素数目为0
			m_nCount.store(0);
			return;
		}
		else
		{
			m_pTail = trail;
		}
	}
	// 删除p节点内存
	delete p;
	// 元素数目减1
	if (m_nCount.fetch_sub(1) == m_nCapacity)
	{
		// 触发队列不满条件
		m_hNotFullCondition.notify_one();
	}
}

template <typename E>
void CMLinkedBlockingQueue<E>::enqueue(E* x)
{
	// 入队列算法移动的是尾节点
	CNode* pNode = new CNode(x);
	m_pTail->_pNext = pNode;
	m_pTail = pNode;
}

template <typename E>
E* CMLinkedBlockingQueue<E>::dequeue()
{
	// 出队列算法移动的是头节点
	if (m_pHead == m_pTail) // 为空
		return nullptr;
	CNode* pH = m_pHead;
	CNode* pFirst = pH->_pNext;
	m_pHead = pFirst;
	E* x = pFirst->_pItem;
	pFirst->_pItem = nullptr;	// 头结点元素为空
	delete pH;					// 清空旧头结点内存
	return x;
}

template <typename E>
void CMLinkedBlockingQueue<E>::Put(E* x)
{
	if (x == nullptr) return;
	int c = -1;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hPutLock, std::defer_lock);
	if (lock.try_lock())
	{
		// 队列满了，则一直等待
		while (this->m_nCount.load() == m_nCapacity)
		{
			m_hNotFullCondition.wait(lock);
		}
		// 队列不满，可加入队列
		enqueue(x);
		// 增加元素数目
		c = this->m_nCount.fetch_add(1);
		// 触发队列不满信号
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.notify_one();
		}
		lock.unlock();
	}
	if (c == 0)
		signalNotEmpty();
}

template <typename E>
bool CMLinkedBlockingQueue<E>::Offer(E* x, long timeout)
{
	if (x == nullptr) return;

	int c = -1;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hPutLock, std::defer_lock);
	if (lock.try_lock())
	{
		// 队列满了
		while (this->m_nCount.load() == m_nCapacity)
		{
			if (!m_hNotFullCondition.wait_for(lock, std::chrono::milliseconds(timeout)))
			{
				// 等待触发条件的时间段已到
				return false;
			}
		}
		// 队列不满，可加入队列
		enqueue(x);
		// 增加元素数目
		c = this->m_nCount.fetch_add(1);
		// 触发队列不满条件
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.notify_one();
		}
		lock.unlock();
	}
	if (c == 0)
		signalNotEmpty();
	return true;
}

template <typename E>
bool CMLinkedBlockingQueue<E>::Offer(E* x)
{
	if (x == nullptr) return;

	if (this->m_nCount.load() == m_nCapacity)
	{
		return false;
	}

	int c = -1;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hPutLock, std::defer_lock);
	lock.lock();
	// 元素数目小于容量了
	if (this->m_nCount.load() < m_nCapacity)
	{
		// 队列不满，可加入队列
		enqueue(x);
		// 增加元素数目
		c = this->m_nCount.fetch_add(1);
		// 触发队列不满条件
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.notify_one();
		}
	}
	lock.unlock();
	if (c == 0)
		signalNotEmpty();
	return c >= 0;
}

template <typename E>
E* CMLinkedBlockingQueue<E>::Take()
{
	E* x = nullptr;
	int c = -1;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hTakeLock, std::defer_lock);
	if (lock.try_lock())
	{
		// 队列为空，则一直等待
		while (this->m_nCount.load() == 0)
		{
			m_hNotEmptyCondition.wait(lock);
		}
		// 队列不空
		x = dequeue();
		// 减少元素数目
		c = this->m_nCount.fetch_sub(1);
		// 触发队列不空条件
		if (c > 1)
		{
			m_hNotEmptyCondition.notify_one();
		}
		lock.unlock();
	}
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CMLinkedBlockingQueue<E>::Poll(long timeout)
{
	E* x = nullptr;
	int c = -1;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hTakeLock, std::defer_lock);
	if (lock.try_lock())
	{
		// 队列为空
		while (this->m_nCount.load() == 0)
		{
			if (!m_hNotEmptyCondition.wait_for(lock, std::chrono::milliseconds(timeout)))
			{
				// 触发不空信号量失败
				return nullptr;
			}
		}
		// 队列不为空
		x = dequeue();
		// 减少元素数目
		c = this->m_nCount.fetch_sub(1);
		// 触发队列不空条件
		if (c > 1)
		{
			m_hNotEmptyCondition.notify_one();
		}
		lock.unlock();
	}
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CMLinkedBlockingQueue<E>::Poll()
{
	if (this->m_nCount.load() == 0)
		return nullptr;

	int c = -1;
	E* x = nullptr;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hTakeLock, std::defer_lock);
	lock.lock();
	// 队列不为空
	if (this->m_nCount.load() > 0)
	{
		x = dequeue();
		c = this->m_nCount.fetch_sub(1);
		// 触发队列不空条件
		if (c > 1)
		{
			m_hNotEmptyCondition.notify_one();
		}

	}
	lock.unlock();
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CMLinkedBlockingQueue<E>::Peek()
{
	if (this->m_nCount.load() == 0) return nullptr;
	std::unique_lock<std::recursive_timed_mutex> lock(m_hTakeLock);
	// 队列为空
	if (m_pHead == m_pTail) return nullptr;
	return m_pHead->_pNext->_pItem;

}

template <typename E>
bool CMLinkedBlockingQueue<E>::Remove(E* x)
{
	if (x == nullptr) return false;
	fullyLock();
	CNode *pTrail, *p;
	for (pTrail = m_pHead, p = pTrail->_pNext;
		p != nullptr;
		pTrail = p, p = p->_pNext)
	{
		if (x == p)
		{
			unlink(p, pTrail);
			fullyUnlock();
			return true;
		}
	}
	fullyUnlock();
	return false;
}

template <typename E>
void CMLinkedBlockingQueue<E>::Clear()
{
	fullyLock();
	CNode* p, *h;
	for (h = m_pHead; (p = h->_pNext) != nullptr; h = p)
	{
		delete p->_pItem;	// 清空元素内存
		delete m_pHead;		// 清空旧头结点
		p->_pItem = nullptr;
		m_pHead = p;		// 赋值新的头结点
	}
	m_pTail = m_pHead;
	// 设置元素数目
	if (this->m_nCount.exchange(0) == m_nCapacity)
	{
		// 触发不满条件
		m_hNotFullCondition.notify_one();
	}
	fullyUnlock();
}


#endif // __MLINKEDBLOCKINGQUEUE_H__