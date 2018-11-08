#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
struct key_comp {
	bool operator()(std::reference_wrapper<const std::string> a,
					std::reference_wrapper<const std::string> b) const {
		return a.get() < b.get();
	}
};

class SimpleLRU : public Afina::Storage {
public:
    SimpleLRU(size_t max_size = 1024) : _max_size(max_size), _curr_size(0), _lru_first(nullptr),
										_lru_last(nullptr){}

    ~SimpleLRU() {
		_lru_index.clear();
		
		while (_lru_first != nullptr){
			std::unique_ptr<lru_node> freed = std::move(_lru_first);
			_lru_first = std::move(freed -> next);
		}
	}

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) const override;

private:
    // LRU cache node
    using lru_node = struct lru_node {
		std::string key;
        std::string value;
		std::unique_ptr<lru_node> next;
        lru_node * prev;
    };

    // Maximum number of bytes could be stored in this cache.
    // i.e all (keys+values) must be less the _max_size
    std::size_t _max_size;
    std::size_t _curr_size;

    // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the last
    // element that wasn't used for longest time.
    //
    // List owns all nodes
    std::unique_ptr<lru_node> _lru_first;
	lru_node * _lru_last;

    // Index of nodes from list above, allows fast random access to elements by lru_node#key
	std::map<std::reference_wrapper<const std::string>,
			 std::reference_wrapper<lru_node>,
			 key_comp> _lru_index;
	// std::map<std::string, std::reference_wrapper<lru_node>> _lru_index;

	bool ReleaseSpace(const size_t size);
	
	void MoveToStart(lru_node & node);
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
