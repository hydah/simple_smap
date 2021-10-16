#include "simple_smap.h"

struct UserKey {
    uint32_t sdkappid_;
    uint32_t roomid_;
    uint64_t tinyid_;

    UserKey(uint32_t sdkappid, uint32_t roomid, uint64_t tinyid)
        : sdkappid_(sdkappid), roomid_(roomid), tinyid_(tinyid) {}

    bool operator==(const UserKey &k) const {
        return sdkappid_ == k.sdkappid_ && roomid_ == k.roomid_ && tinyid_ == k.tinyid_;
    }
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
    bool Validate() { return tinyid != 0; }
    UserKey GetKey() { return UserKey(sdkappid, roomid, tinyid); }
};

struct UserValidate {
    bool operator()(User *u) { return u->Validate(); }
};

struct UserGetKey {
    UserKey operator()(User *u) { return u->GetKey(); }
};

int main() {
    ssmap::simple_smap<User, UserKey, UserValidate, UserGetKey, UserKeyHashFunc> smap;

    int32_t key = 3154070;
    if (!smap.Init(key, 100)) {
        std::cerr << "init key failed" << std::endl;
    }
    UserKey user_key(1, 2, 3);
    User *user = smap.Add(user_key);
    if (user == nullptr) {
        std::cerr << "add user failed" << std::endl;
    }

    if (!smap.Have(user_key)) {
        std::cerr << "can not find user" << std::endl;
    }

    if (!smap.Del(user_key)) {
        std::cerr << "delete faild" << std::endl;
    }
    if (smap.Have(user_key)) {
        std::cerr << "delete faild, still have" << std::endl;
    }
    std::cout << " all success" << std::endl;
    return 0;
}