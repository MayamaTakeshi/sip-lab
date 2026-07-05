use std::collections::{HashMap, VecDeque};

pub struct IdManager {
    available_ids: VecDeque<i64>,
    id_map: HashMap<i64, i64>,
}

impl IdManager {
    pub fn new(max: i64) -> Self {
        let mut available_ids = VecDeque::with_capacity(max as usize);
        for i in 0..max {
            available_ids.push_back(i);
        }
        IdManager {
            available_ids,
            id_map: HashMap::new(),
        }
    }

    pub fn add(&mut self, val: i64) -> Option<i64> {
        if let Some(id) = self.available_ids.pop_front() {
            self.id_map.insert(id, val);
            Some(id)
        } else {
            None
        }
    }

    pub fn remove(&mut self, id: i64) -> Option<i64> {
        if let Some(val) = self.id_map.remove(&id) {
            self.available_ids.push_back(id);
            Some(val)
        } else {
            None
        }
    }

    pub fn remove_by_val(&mut self, val: i64) -> Option<i64> {
        let id = self.id_map.iter()
            .find(|&(_, &v)| v == val)
            .map(|(&k, _)| k);

        if let Some(id) = id {
            self.id_map.remove(&id);
            self.available_ids.push_back(id);
            Some(id)
        } else {
            None
        }
    }

    pub fn get(&self, id: i64) -> Option<i64> {
        self.id_map.get(&id).copied()
    }

    pub fn get_id(&self, val: i64) -> Option<i64> {
        self.id_map.iter()
            .find(|&(_, &v)| v == val)
            .map(|(&k, _)| k)
    }

    pub fn iterate<F>(&self, mut f: F)
    where
        F: FnMut(i64, i64),
    {
        for (&id, &val) in &self.id_map {
            f(id, val);
        }
    }

    pub fn size(&self) -> usize {
        self.available_ids.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_id_manager() {
        let mut manager = IdManager::new(3);
        assert_eq!(manager.size(), 3);

        let id1 = manager.add(10).unwrap();
        let id2 = manager.add(20).unwrap();
        let _id3 = manager.add(30).unwrap();
        assert!(manager.add(40).is_none());

        assert_eq!(manager.get(id1), Some(10));
        assert_eq!(manager.get_id(20), Some(id2));

        manager.remove(id2);
        assert_eq!(manager.size(), 1);
        let id4 = manager.add(40).unwrap();
        assert_eq!(id2, id4);

        manager.remove_by_val(10);
        assert_eq!(manager.get(id1), None);
    }
}
