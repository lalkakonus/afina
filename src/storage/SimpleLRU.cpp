// Kononov Sergey BD-21

#include <memory>
#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string & key, const std::string & value) { 

	size_t node_size = key.size() + value.size();

	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {
		lru_node & node = it->second;
		//auto & node = it->second; // Not working
		
		size_t diff = value.size() - node.value.size();
		if (diff > 0) {
			if (!this->ReleaseSpace(diff)) {
				return false;
			}
		}

		_curr_size += value.size() - node.value.size();
		node.value = value;
		
		MoveToStart(node);
	} else {
		
		if (!this->ReleaseSpace(node_size)) {
			return false;
		}

		lru_node * prev_first = nullptr;

		if (_lru_last == nullptr) {
			//_lru_last.reset(std::make_unique<lru_node>()); // Need to turn on c++14
			_lru_last = std::move(std::unique_ptr<lru_node>(new lru_node));
			_lru_first = _lru_last.get();
		} else {	
			_lru_first->next = std::move(std::unique_ptr<lru_node> (new lru_node));
			prev_first = _lru_first;
			_lru_first = _lru_first->next.get();
		};
	
		_lru_first->next.reset();
		_lru_first->prev = prev_first;
		_lru_first->key = key;
		_lru_first->value = value;	
	
		_curr_size += node_size;
		_lru_index.emplace(key, *_lru_first);
	};
	
	return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) { 

	if (_lru_index.find(key) == _lru_index.end()) {
		return Put(key, value);
	} else {
		return false;
	}
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) { 

	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {

		size_t diff = value.size() - it->second.get().value.size();
		if (diff > 0) {
			if (!this->ReleaseSpace(diff)) {
				return false;
			}
		}
		
		it->second.get().value = value;
		return true;
	} else {
		return false;
	}
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) { 

	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {

		lru_node & node = it->second;
		_curr_size -= node.key.size() + node.value.size();

		if (node.next) {
			node.next->prev = node.prev;
		} else {
			_lru_first = node.prev;
		}

		if (node.prev) {
			node.prev->next = std::move(node.next);
		} else {
			_lru_last = std::move(node.next);
		}

		_lru_index.erase(it);

		return true;
	} else {
		return false;
	}
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) const { 
	
	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {
		value = it->second.get().value;
		return true;
	} else {
		return false;
	}
}

bool SimpleLRU::ReleaseSpace(const size_t size) {
	
	while (size + _curr_size > _max_size) {
		if (_lru_last) {
			_curr_size -= _lru_last->key.size() + _lru_last->value.size();
		} else {
			return false;
		}
		_lru_index.erase(_lru_last->key);
		_lru_last = std::move(_lru_last->next); // similar to  "_lru_last.reset(_lru_last->next.release())"
	}
	return true;
}

void SimpleLRU::MoveToStart(lru_node & node) {
		
		if (node.next == nullptr)
			return;
	
		auto node_owner = node.prev;
		if (node_owner == nullptr)
			node_owner = _lru_first;
	
		node.next->prev = node.prev;
		node.prev = _lru_first;

		node_owner->next.swap(node.next);
		(node.next).swap(_lru_first->next);
		
		_lru_first = _lru_first->next.get();

	return;
}

void SimpleLRU::ClearCache() {

	_lru_index.clear();
	
	while (_lru_last != nullptr){
		std::unique_ptr<lru_node> freed = std::move(_lru_last);
		_lru_last = std::move(freed -> next);
	}
	
	_lru_last.reset();
	
	return;
}

} // namespace Backend
} // namespace Afina
