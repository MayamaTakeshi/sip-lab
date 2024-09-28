#ifndef __IDMANAGER_H__
#define __IDMANAGER_H__

#include <map>

using namespace std;

#include <boost/circular_buffer.hpp>

class IdManager {
public:
  IdManager(long max);
  ~IdManager();
  bool add(long val, long &id);
  bool remove(long id, long &val);
  bool remove_by_val(long val, long &id);
  bool get(long id, long &val);
  bool get_id(long val, long &id);
  void iterate(void (*f)(long id, long val));
  int size();

  map<long, long> id_map; // hack to test tcpdumper
private:
  boost::circular_buffer<long> ids;
};

#endif
