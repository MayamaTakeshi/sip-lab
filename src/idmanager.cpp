#include "idmanager.hpp"

IdManager::IdManager(long max) : ids(max) {
  for (long i = 0; i < max; ++i)
    ids.push_back(i);
};

IdManager::~IdManager(){};

bool IdManager::add(long val, long &id) {
  if (ids.empty())
    return false;

  long new_id = ids[0];
  ids.pop_front();

  id_map[new_id] = val;
  id = new_id;
  return true;
}

bool IdManager::remove(long id, long &val) {
  map<long, long>::iterator pos = id_map.begin();
  while (pos != id_map.end()) {
    if (pos->first == id) {
      val = pos->second;
      ids.push_back(id);
      id_map.erase(pos);
      return true;
    }
    ++pos;
  }
  return false;
}

bool IdManager::remove_by_val(long val, long &id) {
  map<long, long>::iterator pos = id_map.begin();
  while (pos != id_map.end()) {
    if (pos->second == val) {
      id = pos->first;
      ids.push_back(id);
      id_map.erase(pos);
      return true;
    }
    ++pos;
  }
  return false;
}

bool IdManager::get(long id, long &val) {
  map<long, long>::iterator pos = id_map.find(id);
  if (pos == id_map.end())
    return false;

  val = pos->second;
  return true;
}

bool IdManager::get_id(long val, long &id) {
  map<long, long>::iterator pos = id_map.begin();
  while (pos != id_map.end()) {
    if (pos->second == val) {
      id = pos->first;
      return true;
    }
    ++pos;
  }
  return false;
}

void IdManager::iterate(void (*f)(long id, long val)) {
  map<long, long>::iterator pos = id_map.begin();
  while (pos != id_map.end()) {
    f(pos->first, pos->second);
    ++pos;
  }
}

int IdManager::size() {
    return ids.size();
}
