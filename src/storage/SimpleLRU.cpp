#include <memory>
#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string & key, const std::string & value) { 

	// Size of current node
	size_t node_size = key.size() + value.size();

	// Check if key present in cache
	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {

		// If file present in cache move node to tail
		lru_node & node = it->second;
		//auto & node = it->second; // Not working

		if (node.value.size() < value.size()) {
			if (!this->release_space(value.size() - node.value.size())) {
				return false;
			}
		}

		_current_size += value.size() - node.value.size();
		node.value = value;
		
		if (node.next == nullptr)
			return true;
	
		auto node_owner = node.prev;
		if (node_owner == nullptr)
			node_owner = _last;
	
		// Change links to previous nodes
		node.next->prev = node.prev;
		node.prev = _last;

		// Change links to next nodes
		node_owner->next.swap(node.next);
		(node.next).swap(_last->next);
		
		// Move link to last to forward node
		_last = _last->next.get();
	} else {
		
		if (!this->release_space(node_size)) {
			return false;
		}

		lru_node * prev_last = nullptr;

		if (_lru_head == nullptr) {
			//_lru_head.reset(std::make_unique<lru_node>()); // Need to turn on c++14
			_lru_head = std::move(std::unique_ptr<lru_node>(new lru_node));
			_last = _lru_head.get();
		} else {	
			_last->next = std::move(std::unique_ptr<lru_node> (new lru_node));
			prev_last = _last;
			_last = _last->next.get();
		};
	
		_last->next.reset();
		_last->prev = prev_last;
		_last->key = key;
		_last->value = value;	
	
		_curr_size += node_size;
		//_lru_index[key] = *_last; // Not working
		_lru_index.emplace(key, *_last);
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

		node.next->prev = node.prev;
		node.prev->next = std::move(node.next);
		
		_lru_index.erase(it);

		////
		/*
		auto old_head = std::move(lru_head);
		_lru_head = std::move(second.get().prev->next)
		second.get().prev->next = std::move(lru_head->next)
		lru_head->next = std::move(old_head);
		*/
		////

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

} // namespace Backend
} // namespace Afina
