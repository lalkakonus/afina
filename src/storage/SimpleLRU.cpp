#include <memory>
#include "SimpleLRU.h"
#include <iostream>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string & key, const std::string & value) { 

	std::cout << "### Put key/value in cache ###\n" ;

	// Size of current node
	size_t node_size = key.size() + value.size();

	// Check if key present in cache
	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {
		// If file present in cache move node to tail
		std::cout << "Current node exist in list.\nMove it to end.\n";
		
		lru_node & node = it -> second;

		if (node.next == nullptr)
			return true;
	
		auto node_owner = node.prev;
		if (node_owner == nullptr)
			node_owner = _last;
	
		// Change links to previous nodes
		node.next -> prev = node.prev;
		node.prev = _last;

		// Change links to next nodes
		node_owner -> next.swap(node.next);
		(node.next).swap(_last -> next);
		
		// Move link to last to forward node
		_last = _last -> next.get();
	}
	else {
		// If current node not exist in list check free space
		std::cout << "Current node not exist in list.\nCheck free space.\n";

		while (node_size + _curr_size > _max_size){
			// if there is not enough space delete oldest element
			if (_lru_head != nullptr)
				_curr_size -= _lru_head -> key.size() + _lru_head -> value.size();
			else
				return false;
			_lru_head = std::move(_lru_head -> next); // similar to  "_lru_head.reset(_lru_head -> next.release())"
		}

		std::cout << "Free space is enought.\n";

		lru_node * prev_last = nullptr;

		if (_lru_head == nullptr) {
			// Insert new node in case of empty doubly linked list
			std::cout << "Insert new node in case of empty doubly linked list\n";
			//_lru_head.reset(std::make_unique<lru_node>()); // Need to turn on c++14
			_lru_head = std::move(std::unique_ptr<lru_node>(new lru_node));
			_last = _lru_head.get();
		}
		else {	
			// Insert node to end in case of non empty doubly linked list
			std::cout << "Insert node to end in case of non empty doubly linked list\n";
			_last -> next = std::move(std::unique_ptr<lru_node> (new lru_node));
			prev_last = _last;
			_last = _last -> next.get();
		};
	
		// Create new node at the end of list
		std::cout << "Create new node at the end of list\n";
		_last -> next.reset();
		_last -> prev = prev_last;
		_last -> key = key;
		_last -> value = value;	
	
		// increase currnet size of container
		std::cout << "Increase currnet size of container\n";
		_curr_size += node_size;
		// add key/value pair to list of contents
		//_lru_index[key] = *_last; // Not working
		std::cout << "Add key/value pair to list of contents\n";
		_lru_index.emplace(key, *_last);
	};
	
	std::cout << "Addition succses.\n";

	return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) { 

	if (_lru_index.find(key) == _lru_index.end())
		return Put(key, value);
	else
		return false;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) { 

	auto it = _lru_index.find(key);

	if (it == _lru_index.end()) {
		it -> second.get().value = value;
		return true;
	}
	else
		return false;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) { 

	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {
		
		lru_node & node = it -> second;
		//auto node = it -> second; // Not working

		node.next -> prev = node.prev;
		node.prev -> next = std::move(node.next);
		
		_lru_index.erase(it);
		return true;
	}
	else
		return false;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) const { 
	
	auto it = _lru_index.find(key);

	if (it != _lru_index.end()) {
		value = it -> second.get().value;
		return true;
	}
	else
		return false;
}

} // namespace Backend
} // namespace Afina
