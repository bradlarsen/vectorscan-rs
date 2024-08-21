mod error;
mod native;
mod wrapper;

pub use error::{AsResult, Error, HyperscanErrorCode};
pub use native::*;
pub use wrapper::{Flag, Pattern, ScanMode, Scratch};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic() -> Result<(), Error> {
        let patterns = vec![
            Pattern::new(b"hello".to_vec(), Flag::default(), None),
        ];
        let db = BlockDatabase::new(patterns)?;
        let mut scanner = BlockScanner::new(&db)?;

        let test_basic = |scanner: &mut BlockScanner| -> Result<(), Error> {
            let mut matches = Vec::new();
            scanner.scan(b"hello hello", |id: u32, from: u64, to: u64, flags: u32| {
                matches.push((id, from, to, flags));
                Scan::Continue
            })?;
            assert_eq!(matches.as_slice(), &[(0, 0, 5, 0), (0, 0, 11, 0)]);
            Ok(())
        };

        test_basic(&mut scanner)?;

        // Make sure cloning works
        let mut scanner2 = scanner.clone();
        drop(scanner);
        test_basic(&mut scanner2)?;

        Ok(())
    }

    #[should_panic]
    #[test]
    fn empty_database() {
        let _db = wrapper::Database::new(vec![], ScanMode::BLOCK).unwrap();
    }

    #[test]
    fn clone_database() -> Result<(), Error> {
        let patterns = vec![
            Pattern::new(b"hello".to_vec(), Flag::default(), None),
        ];

        let db = wrapper::Database::new(patterns, ScanMode::BLOCK)?;
        let _dbs: Vec<wrapper::Database> = (0..100).map(|_| db.clone()).collect();
        drop(db);
        Ok(())
    }

    #[test]
    fn database_size() -> Result<(), Error> {
        let patterns = vec![
            Pattern::new(b"hello".to_vec(), Flag::default(), None),
        ];

        let db = wrapper::Database::new(patterns, ScanMode::BLOCK)?;
        let db_size = db.size()?;
        assert_eq!(db_size, 936); // XXX this value may change if the vectorscan code changes

        let sdb = db.serialize()?;
        let sdb_size = sdb.deserialized_size()?;
        assert_eq!(db_size, sdb_size);

        let db2 = sdb.deserialize()?;
        let db2_size = db2.size()?;
        assert_eq!(db2_size, db_size);

        Ok(())
    }
}
