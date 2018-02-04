#ifndef __XXMLINKEDBLOCKINGQUEUE_H__
#define __XXMLINKEDBLOCKINGQUEUE_H__

/////////////////////////////////////////////////////////////////////////
// monolith: ���ι������
// CXXMLinkedBlockingQueue: ��������ʵ�ֵ���������
#include <atomic>
#include <limits.h>
#include "MLock.h"

template <typename E>
class CXXMLinkedBlockingQueue
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
	CXXMLinkedBlockingQueue() :CXXMLinkedBlockingQueue(INT_MAX) {}
	CXXMLinkedBlockingQueue(int capacity)
		:m_nCapacity(capacity)
	{
		m_pHead = new CNode;	// Ԫ��Ϊ�յ�ͷָ��
		m_pTail = m_pHead;		// ��ʼʱ��β�ڵ����ͷ���
		m_nCount.store(0);		// ��ʼԪ����ĿΪ0
	}
	~CXXMLinkedBlockingQueue()
	{
		// 1.���Ԫ��
		Clear();
		// 2.ɾ��ͷ����ڴ�
		delete m_pHead;
		m_pTail = m_pHead = nullptr;
	}

public:
	// ��ȡԪ����Ŀ
	inline unsigned int Size() const { return m_nCount.load(); }
	// ��ȡʣ������
	inline unsigned int GetRemainCapacity() const { return m_nCapacity - m_nCount.load(); }
	// ����Ԫ�ص�����β��
	void Put(E* x);
	// ����Ԫ�ص�����β��������������ˣ���ȴ������ĺ���ʱ��
	// �ȴ�����ʱ��������򷵻�false
	bool Offer(E* x, long timeout);
	// ����Ԫ�ص�����β��������������ˣ��򷵻�false
	bool Offer(E* x);
	// �Ӷ����ײ�ȡԪ��
	E* Take();
	// �Ӷ����ײ�ȡԪ�أ��������Ϊ�գ���ȴ�����������
	// �ȴ�ʱ�䵽���Ϊ�գ��򷵻�null
	E* Poll(long timeout);
	// �Ӷ����ײ�ȡԪ�أ��������Ϊ�գ��򷵻�null
	E* Poll();
	// �õ���һ��Ԫ��
	E* Peek();
	// �Ƴ�ĳ��Ԫ��
	bool Remove(E* x);
	// ���Ԫ��
	void Clear();
protected:
	// ȡ��ĳ���ڵ������
	void unlink(CNode* p, CNode* trail);

	// �Բ�������ȫ����
	inline void fullyLock()
	{
		m_hPutLock.lock();
		m_hTakeLock.lock();
	}

	// �Բ�������ȫ����
	inline void fullyUnlock()
	{
		m_hTakeLock.unlock();
		m_hPutLock.unlock();
	}
private:
	// ����take/offer�ȴ��ź���
	inline void signalNotEmpty()
	{
		CMMutexScoped lock(m_hTakeLock);
		m_hNotEmptyCondition.signal();
	}

	// ����put/poll�ȴ��ź���
	inline void signalNotFull()
	{
		CMMutexScoped lock(m_hPutLock);
		m_hNotFullCondition.signal();
	}

	// ����һ���ڵ㣬���ӵ�����β��
	void enqueue(E* x);
	// ��ͷ���Ƴ�һ���ڵ㣬�����Ƴ��ڵ��Ԫ��
	E* dequeue();

private:
	// ��������	// Ĭ�����int
	int		m_nCapacity;
	// ����Ԫ����Ŀ ԭ�Ӳ���
	std::atomic_int	m_nCount;
	// ����Ԫ�ر�ʶ
	// ��ʼʱ��ͷָ���βָ�붼ָ��ͬһ��ֵΪnull�Ľڵ�
	CNode*				m_pHead;	// ͷ�ڵ�	���ӽڵ�
	CNode*				m_pTail;	// β�ڵ�	nextָ��ָ������
	// ������
	CMReentrantMutex	m_hTakeLock;	// take,poll�Ȳ������е���
	CMReentrantMutex	m_hPutLock;		// put,offer�Ȳ������е���
	CMCondition			m_hNotEmptyCondition;	// take������Ҫ�ȴ�������
	CMCondition			m_hNotFullCondition;	// put������Ҫ�ȴ�������
};


template <typename E>
void CXXMLinkedBlockingQueue<E>::unlink(CNode* p, CNode* trail)
{
	trail->_pNext = p->_pNext;
	if (m_pTail == p)
	{
		if (m_pHead->_pNext == p)	// Ψһ��һ���ڵ�
		{
			// ��֤ͷ�ڵ���Ԫ��Ϊ�յĽڵ�
			delete p->_pItem;	// ɾ��Ԫ���ڴ�
			p->_pItem = nullptr;
			// ��ʱԪ����ĿΪ0
			m_nCount.store(0);
			return;
		}
		else
		{
			m_pTail = trail;
		}
	}
	// ɾ��p�ڵ��ڴ�
	delete p;
	// Ԫ����Ŀ��1
	if (m_nCount.fetch_sub(1) == m_nCapacity)
	{
		// �������в�������
		m_hNotFullCondition.signal();
	}
}

template <typename E>
void CXXMLinkedBlockingQueue<E>::enqueue(E* x)
{
	// ������㷨�ƶ�����β�ڵ�
	CNode* pNode = new CNode(x);
	m_pTail->_pNext = pNode;
	m_pTail = pNode;
}

template <typename E>
E* CXXMLinkedBlockingQueue<E>::dequeue()
{
	// �������㷨�ƶ�����ͷ�ڵ�
	if (m_pHead == m_pTail) // Ϊ��
		return nullptr;
	CNode* pH = m_pHead;
	CNode* pFirst = pH->_pNext;
	m_pHead = pFirst;
	E* x = pFirst->_pItem;
	pFirst->_pItem = nullptr;	// ͷ���Ԫ��Ϊ��
	delete pH;					// ��վ�ͷ����ڴ�
	return x;
}

template <typename E>
void CXXMLinkedBlockingQueue<E>::Put(E* x)
{
	if (x == nullptr) return;
	int c = -1;
	if (m_hPutLock.trylock() == 0)
	{// �������ˣ���һֱ�ȴ�
		while (m_nCount.load() == m_nCapacity)
		{
			m_hNotFullCondition.wait(&m_hPutLock);
		}
		// ���в������ɼ������
		enqueue(x);
		// ����Ԫ����Ŀ
		c = m_nCount.fetch_add(1);
		// m_nCount++;
		// �������в����ź�
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.signal();
		}
		m_hPutLock.unlock();
	}
	if (c == 0)
		signalNotEmpty();
}

template <typename E>
bool CXXMLinkedBlockingQueue<E>::Offer(E* x, long timeout)
{
	if (x == nullptr) return;

	int c = -1;
	if (m_hPutLock.trylock() == 0)
	{
		// ��������
		while (m_nCount.load() == m_nCapacity)
		{
			if (m_hNotFullCondition.wait(&m_hPutLock, timeout) != 0)
			{
				// �ȴ�����������ʱ����ѵ�
				return false;
			}
		}
		// ���в������ɼ������
		enqueue(x);
		// ����Ԫ����Ŀ
		c = m_nCount.fetch_add(1);
		// �������в�������
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.signal();
		}
		m_hPutLock.unlock();
	}
	if (c == 0)
		signalNotEmpty();
	return true;
}

template <typename E>
bool CXXMLinkedBlockingQueue<E>::Offer(E* x)
{
	if (x == nullptr) return;

	if (m_nCount.load() == m_nCapacity)
	{
		return false;
	}

	int c = -1;
	m_hPutLock.Lock();
	// Ԫ����ĿС��������
	if (m_nCount.load() < m_nCapacity)
	{
		// ���в������ɼ������
		enqueue(x);
		// ����Ԫ����Ŀ
		c = m_nCount.fetch_add(1);
		// �������в�������
		if (c + 1 < m_nCapacity)
		{
			m_hNotFullCondition.signal();
		}
	}
	m_hPutLock.unlock();
	if (c == 0)
		signalNotEmpty();
	return c >= 0;
}

template <typename E>
E* CXXMLinkedBlockingQueue<E>::Take()
{
	E* x = nullptr;
	int c = -1;
	if (m_hTakeLock.trylock() == 0)
	{
		// ����Ϊ�գ���һֱ�ȴ�
		while (m_nCount.load() == 0)
		{
			m_hNotEmptyCondition.wait(&m_hTakeLock);
		}
		// ���в���
		x = dequeue();
		// ����Ԫ����Ŀ
		c = m_nCount.fetch_sub(1);
		// �������в�������
		if (c > 1)
		{
			m_hNotEmptyCondition.signal();
		}
		m_hTakeLock.unlock();
	}
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CXXMLinkedBlockingQueue<E>::Poll(long timeout)
{
	E* x = nullptr;
	int c = -1;
	if (m_hTakeLock.trylock() == 0)
	{
		// ����Ϊ��
		while (m_nCount.load() == 0)
		{
			if (m_hNotEmptyCondition.wait(&m_hTakeLock, timeout) != 0)
			{
				// ���������ź���ʧ��
				return nullptr;
			}
		}
		// ���в�Ϊ��
		x = dequeue();
		// ����Ԫ����Ŀ
		c = m_nCount.fetch_sub(1);
		// �������в�������
		if (c > 1)
		{
			m_hNotEmptyCondition.signal();
		}
		m_hTakeLock.unlock();
	}
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CXXMLinkedBlockingQueue<E>::Poll()
{
	if (m_nCount.load() == 0)
		return nullptr;

	int c = -1;
	E* x = nullptr;
	m_hTakeLock.Lock();
	// ���в�Ϊ��
	if (m_nCount.load() > 0)
	{
		x = dequeue();
		c = m_nCount.fetch_sub(1);
		// �������в�������
		if (c > 1)
		{
			m_hNotEmptyCondition.signal();
		}

	}
	m_hTakeLock.unlock();
	if (c == m_nCapacity)
		signalNotFull();
	return x;
}

template <typename E>
E* CXXMLinkedBlockingQueue<E>::Peek()
{
	if (m_nCount.load() == 0) return nullptr;
	CMMutexScoped lock(m_hTakeLock);
	// ����Ϊ��
	if (m_pHead == m_pTail) return nullptr;
	return m_pHead->_pNext->_pItem;

}

template <typename E>
bool CXXMLinkedBlockingQueue<E>::Remove(E* x)
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
void CXXMLinkedBlockingQueue<E>::Clear()
{
	fullyLock();
	CNode* p, *h;
	for (h = m_pHead; (p = h->_pNext) != nullptr; h = p)
	{
		delete p->_pItem;	// ���Ԫ���ڴ�
		delete m_pHead;		// ��վ�ͷ���
		p->_pItem = nullptr;
		m_pHead = p;		// ��ֵ�µ�ͷ���
	}
	m_pTail = m_pHead;
	// ����Ԫ����Ŀ
	if (m_nCount.exchange(0) == m_nCapacity)
	{
		// ������������
		m_hNotFullCondition.signal();
	}
	fullyUnlock();
}

#endif // __MLINKEDBLOCKINGQUEUE_H__