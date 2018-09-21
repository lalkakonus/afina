#include <memory>
#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string & key, const std::string & value) { 

	// Size of current node
	size_t node_size = key.size() + value.size();

	// Check if key present in cache
	std::string k = "key";
	//std::reference_wrapper<std::string> r {key};
	auto it = _lru_index.find(k);

	if (it != _lru_index.end()){
		// If file present in cache move node to tail
		
		lru_node & node = (it -> second);
	
		lru_node * node_owner = node.prev;

		// Change links to previous nodes
		node.next -> prev = node.prev;
		node.prev = _last;
		
		// Change links to next nodes
		(node_owner -> next).swap(node.next);
		(node.next).swap(_last -> next);

		// Move link to last to forward node
		_last = (_last -> next).get();
	}
	else{
		// If current node not exist in list check free space
		while (node_size + _curr_size > _max_size){
			// if there is not enough space delete oldest element
			if (_lru_head != nullptr)
				_curr_size -= (_lru_head -> key).size() + (_lru_head -> value).size();
			else
				return false;
			_lru_head = std::move(_lru_head -> next); // similar to  "_lru_head.reset(_lru_head -> next.release())"
		}

		lru_node * prev_last = nullptr;

		// Insert new node in case of empty doubly linked list
		if (_lru_head == nullptr){
			//_lru_head.reset(std::make_unique<lru_node>());
			_lru_head = std::move(std::unique_ptr<lru_node>(new lru_node));
			_last = _lru_head.get();
		}
		else{	
			// Insert node to end in case of non empty doubly linked list
			//(_last -> next).reset(std::make_unique<lru_node>());
			(_last -> next) = std::move(std::unique_ptr<lru_node>(new lru_node));
			prev_last = _last;
			_last = (_last -> next).get();
		};
	
		// Create new node at the end of list
		(_last -> next).reset();
		_last -> prev = prev_last;
		(_last -> key) = key;
		(_last -> value) = value;	
	
		// increase currnet size of container
		_curr_size += node_size;
		// add key/value pair to list of contents
		_lru_index[key] = std::ref(*_last);
	};

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

	if (it == _lru_index.end()){
		(it -> second).value = value;
		return true;
	}
	else
		return false;

}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key){ 

	auto it = _lru_index.find(key);

	if (it == _lru_index.end()){
		
		lru_node & node = it -> second;

		node.next -> prev = node.prev;
		node.prev -> next = std::move(node.next);
		return true;
	}
	else
		return false;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) const { 
	
	auto it = _lru_index.find(key);

	if (it == _lru_index.end()){
		value = (it -> second).value;
		return true;
	}
	else
		return false;
}

} // namespace Backend
} // namespace Afina
