#pragma once

#include <stdarg.h>
#include <string.h>
#include <sys/shm.h>
#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>

namespace std {
template <typename _Tp>
struct __Validate;
template <typename T>
struct __Validate<T *> {
    bool operator()(T *t) const noexcept { return reinterpret_cast<bool>(t); }
};

template <typename T, typename Key>
struct __GetKey;
template <typename T, typename Key>
struct __GetKey<T *, Key> {
    Key operator()(T *t) const noexcept { return reinterpret_cast<Key>(t); }
};

template <typename T, typename Key>
struct __ImpWithKey;
template <typename T, typename Key>
struct __ImpWithKey<T *, Key> {
    bool operator()(T *t, Key key) const noexcept { return false; }
};
}  // namespace std

namespace smap {
#pragma pack(1)
template <typename T>
struct SmapLinkList {
    T info;
    uint32_t next_block;
};
#pragma pack()

template <typename T, typename Key, typename _validate = std::__Validate<T *>,
          typename _get_key = std::__GetKey<T *, Key>,
          typename _imp_with_key = std::__ImpWithKey<T *, Key>, typename _Hash = std::hash<Key>>
class Smap {
 public:
    bool Init(int32_t shm_key, uint32_t info_num) {
        data_num_ = info_num;
        size_t shm_size = (info_num + 1) * sizeof(SmapLinkList<T>);  // 多申请一个，头不占用
        std::cout << s_printf("info shm key:%u(0x%x): InfoLinkList size: %u, shm size: %u", shm_key,
                              shm_key, sizeof(SmapLinkList<T>), shm_size)
                  << std::endl;

        bool is_new = false;
        int32_t *shm_head = nullptr;
        int32_t ret = GetInfoListShm((void **)&shm_head, shm_key, shm_size, 0666);
        if (ret < 0) {
            ret = GetInfoListShm((void **)&shm_head, shm_key, shm_size, 0644 | IPC_CREAT);
            is_new = true;
        }
        if (!shm_head || ret < 0) {
            if (is_new) {
                std::cerr << "add new" << std::endl;
            } else {
                std::cerr << "attach" << std::endl;
            }
            return false;
        }

        info_head_ = reinterpret_cast<SmapLinkList<T> *>(shm_head);
        if (!is_new) {
            std::cerr << "attach shm" << std::endl;
            info_head_->next_block = kInvalidIndex;
            // 初始化map
            SmapLinkList<T> *cur_node = nullptr;
            uint32_t free_num = 0;
            for (int32_t i = info_num; i > 0; i--) {
                cur_node = info_head_ + i;
                if (_validate()(&cur_node->info)) {
                    Key key = _get_key()(&(cur_node->info));
                    info_map_.emplace(key, i);
                    cur_node->next_block = kInvalidIndex;
                } else {
                    cur_node->next_block = info_head_->next_block;
                    info_head_->next_block = i;
                    free_num++;
                }
            }

            if (free_num + info_map_.size() != info_num) {
                std::cerr << "shm corrupted " << free_num << " " << info_map_.size() << std::endl;
                return false;
            }

            return true;
        } else {
            std::cerr << "creat new shm" << std::endl;
            memset(info_head_, 0, shm_size);
            info_head_->next_block = kInvalidIndex;

            for (int32_t i = info_num; i > 0; i--) {
                SmapLinkList<T> *info_node = info_head_ + i;
                info_node->next_block = info_head_->next_block;
                info_head_->next_block = i;
            }
            info_map_.clear();
        }

        return true;
    }

    void RebuildShmList() {
        info_head_->next_block = kInvalidIndex;
        for (int32_t i = data_num_; i > 0; i--) {
            auto cur_node = info_head_ + i;
            T *info = &cur_node->info;

            if (info_map_.count(_get_key()(info)) != 0) continue;

            memset(info, 0, sizeof(*info));
            cur_node->next_block = info_head_->next_block;
            info_head_->next_block = i;
        }
    }

    T *Add(Key key) {
        std::cout << "key is " << key << std::endl;
        if (info_map_.find(key) != info_map_.end()) {
            auto index = info_map_[key];
            SmapLinkList<T> *node = info_head_ + index;
            return &node->info;
        }

        auto free_idx = info_head_->next_block;

        if (free_idx == kInvalidIndex) {
            // 当出现不能分配时, 先尝试恢复下, 如果恢复完后仍然分配不了, 则分配失败
            auto pre_idx = free_idx;
            RebuildShmList();

            free_idx = info_head_->next_block;

            std::cerr
                << s_printf(
                       "free idx=%u, info_map size=%u, try rebuild shm mem list, after free idx=%u",
                       pre_idx, info_map_.size(), free_idx)
                << std::endl;

            if (free_idx == kInvalidIndex) {
                // rebuild 后, 仍然没有空闲节点, 则直接返回空, 分配失败
                std::cerr << s_printf("no free shm block, info_map size=%u", info_map_.size())
                          << std::endl;
                return nullptr;
            }
        }

        SmapLinkList<T> *node = info_head_ + free_idx;
        T *info = &node->info;
        if (info_map_.count(_get_key()(info)) != 0) {
            std::cerr << "key " << _get_key()(info) << "has exist"
                      << s_printf("alloc index=%u, rebuild", free_idx) << std::endl;
            // 当前拿到的free index, 在sh map里有效, 说明出现了回环. rebuild
            // Head->a->b->c->a, Head->a->a
            RebuildShmList();

            free_idx = info_head_->next_block;
            if (free_idx == kInvalidIndex) {
                std::cerr << s_printf("after rebuild, no free user shm block, user_map size=%u",
                                      info_map_.size())
                          << std::endl;
                return nullptr;  //没有空闲节点
            } else {
                node = info_head_ + free_idx;
                T *info = &node->info;
            }
        }

        info_head_->next_block = node->next_block;
        node->next_block = kInvalidIndex;
        memset(info, 0, sizeof(*info));
        _imp_with_key()(info, key);
        info_map_.emplace(key, free_idx);
        std::cerr << s_printf("add info: %u", free_idx) << std::endl;
        return info;
    }

    bool Del(Key key) {
        if (!Have(key)) return false;

        int32_t index = info_map_[key];
        if (index < 0 || index >= data_num_) {
            return false;
        }

        auto info = GetInfo(index);
        if (!ClearFromShm(info)) return false;

        info_map_.erase(key);
        std::cerr << "del :" << index << std::endl;
        return true;
    }

    bool ClearFromShm(T *info) {
        if (!info) return false;

        memset(info, 0, sizeof(*info));
        SmapLinkList<T> *node = reinterpret_cast<SmapLinkList<T> *>(info);
        node->next_block = info_head_->next_block;
        auto index = reinterpret_cast<SmapLinkList<T> *>(info) - info_head_;
        info_head_->next_block = index;

        return true;
    }

    uint32_t GetIndex(T *info) {
        auto index = reinterpret_cast<SmapLinkList<T> *>(info) - info_head_;
        return index;
    }

    // map operators
    bool EraseKey(Key key) { return info_map_.erase(key); }
    bool Have(Key key) {
        if (info_map_.find(key) != info_map_.end()) return true;
        return false;
    }
    T *Get(Key key) {
        if (!Have(key)) return nullptr;
        auto idx = info_map_[key];
        return GetInfo(idx);
    }

    typename std::unordered_map<Key, uint32_t, _Hash>::iterator Begin() {
        return info_map_.begin();
    }
    typename std::unordered_map<Key, uint32_t, _Hash>::iterator End() { return info_map_.end(); }
    typename std::unordered_map<Key, uint32_t, _Hash>::iterator Erase(
        typename std::unordered_map<Key, uint32_t, _Hash>::iterator it) {
        return info_map_.erase(it);
    }

    typename std::unordered_map<Key, uint32_t, _Hash>::iterator DelInfo(
        typename std::unordered_map<Key, uint32_t, _Hash>::iterator it) {
        if (it == info_map_.end()) return info_map_.end();

        auto idx = it->second;
        auto info = GetInfo(idx);

        ClearFromShm(info);

        return info_map_.erase(it);
    }

    T *GetInfo(typename std::unordered_map<Key, uint32_t, _Hash>::iterator it) {
        if (it == info_map_.end()) return nullptr;

        auto idx = it->second;
        return GetInfo(idx);
    };
    T *GetInfo(int32_t index) {
        SmapLinkList<T> *node = info_head_ + index;
        return &(node->info);
    };

    T *GetInfoWithKey(Key key) {
        if (!Have(key)) return nullptr;
        auto idx = info_map_[key];
        return GetInfo(idx);
    }

 private:
    std::string s_printf(const char *fmt, ...) {
        static __thread char ssmap_ostr[10 * 1024] = {0};

        va_list ap;
        va_start(ap, fmt);
        auto wlen = vsnprintf(ssmap_ostr, sizeof(ssmap_ostr) - 1, fmt, ap);
        va_end(ap);
        if (wlen < 0) return "";
        ssmap_ostr[wlen] = '\0';

        return ssmap_ostr;
    };
    char *GetShm(int32_t key, int32_t size, int32_t flag) {
        int shmid;
        char *shm;

        if (0 == key) {
            std::cerr << s_printf("shmget %d %d: iKey don't zero(0)", key, size) << std::endl;
            return NULL;
        }

        if ((shmid = shmget(key, size, flag)) < 0) {
            std::cerr << s_printf("shmget %d %d", key, size) << std::endl;
            return NULL;
        }

        if ((shm = (char *)shmat(shmid, NULL, 0)) == (char *)-1) {
            std::cerr << "shmat" << std::endl;
            return NULL;
        }

        return shm;
    };
    int GetInfoListShm(void **p_shm, int32_t key, int32_t size, int32_t flag) {
        char *shm;
        if (0 == key) {
            return (-1);
        }

        if (!(shm = GetShm(key, size, flag & (~IPC_CREAT)))) {
            if (!(flag & IPC_CREAT)) {
                return -1;
            }
            if (!(shm = GetShm(key, size, flag))) {
                return -1;
            }
            memset(shm, 0, size);
            *p_shm = shm;
            return (1);
        }

        *p_shm = shm;
        return 0;
    }

    SmapLinkList<T> *info_head_;
    std::unordered_map<Key, uint32_t, _Hash> info_map_;
    uint32_t data_num_;
    const int32_t kInvalidIndex = 0;
};
}  // namespace smap
