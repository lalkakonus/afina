#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
class SimpleLRU : public Afina::Storage {
public:
    SimpleLRU(size_t max_size = 1024) : _max_size(max_size), _curr_size(0), _lru_first(nullptr),
										_lru_last(nullptr){}

    ~SimpleLRU() {
    	ClearCache();
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
    std::unordered_map<std::string, std::reference_wrapper<lru_node>> _lru_index;

	bool ReleaseSpace(const size_t size);
	
	void MoveToStart(lru_node & node);

	void ClearCache();
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
