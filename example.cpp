#include <ostream>
#include "simple_smap.h"
struct UserKey {
    uint32_t sdkappid_;
    uint32_t roomid_;
    uint64_t tinyid_;
    UserKey() : sdkappid_(0), roomid_(0), tinyid_(0) {}
    UserKey(uint32_t sdkappid, uint32_t roomid, uint64_t tinyid)
        : sdkappid_(sdkappid), roomid_(roomid), tinyid_(tinyid) {}
    friend std::ostream &operator<<(std::ostream &os, UserKey &key);

    bool operator==(const UserKey &k) const {
        return sdkappid_ == k.sdkappid_ && roomid_ == k.roomid_ && tinyid_ == k.tinyid_;
    };

    std::string ToString() {
        return std::to_string(sdkappid_) + "-" + std::to_string(roomid_) + "-" +
               std::to_string(tinyid_);
    }
};
std::ostream &operator<<(std::ostream &os, UserKey &key) {
    os << key.ToString();
    return os;
};


struct UserKeyHashFunc {
    uint64_t operator()(const UserKey &k) const {
        uint64_t result = 17;
        result = result * 31 + std::hash<uint32_t>()(k.sdkappid_);
        result = result * 31 + std::hash<uint32_t>()(k.roomid_);
        result = result * 31 + std::hash<uint64_t>()(k.tinyid_);
        return result ^ (result >> 32);
    }
};

struct User {
    uint64_t tinyid;
    uint32_t roomid;
    uint32_t sdkappid;

    uint32_t random;
    bool Validate() {
        // std::cout << "tinyid: " << tinyid << std::endl;
        return tinyid != 0;
    }
    UserKey GetKey() { return UserKey(sdkappid, roomid, tinyid); }
    bool CopyFromKey(UserKey key) {
        tinyid = key.tinyid_;
        roomid = key.roomid_;
        sdkappid = key.sdkappid_;
        return true;
    }
};

struct UserValidate {
    bool operator()(User *u) { return u->Validate(); }
};

struct UserGetKey {
    UserKey operator()(User *u) { return u->GetKey(); }
};

struct ImplementWithKey {
    bool operator()(User *u, UserKey key) { return u->CopyFromKey(key); }
};

int main() {
    smap::Smap<User, UserKey, UserValidate, UserGetKey, ImplementWithKey, UserKeyHashFunc> smap;

    int32_t key = 3154070;
    if (!smap.Init(key, 100)) {
        std::cerr << "init key failed" << std::endl;
    }
    UserKey user_key(1, 2, 3);
    // User *user = smap.Add(user_key);
    // if (user == nullptr) {
    //     std::cerr << "add user failed" << std::endl;
    // }

    // if (!smap.Have(user_key)) {
    //     std::cerr << "can not find user" << std::endl;
    // }

    // if (!smap.Del(user_key)) {
    //     std::cerr << "delete faild" << std::endl;
    // }
    // if (smap.Have(user_key)) {
    //     std::cerr << "delete faild, still have" << std::endl;
    // }
    // std::cout << " all success" << std::endl;

    smap.Add(user_key);
    smap.Add(UserKey(2, 3, 4));
    for (auto it = smap.Begin(); it != smap.End();) {
        auto key = it->first;
        std::cout << "key: " << key.ToString() << " value: " << smap.GetInfo(it)->tinyid
                  << std::endl;
        // if (smap.GetInfo(it)->tinyid == 3) {
        //     smap.DelInfo(it++);
        //     continue;
        // }
        ++it;
    }

    return 0;
}