//! Arc-wrapped database and owned scanner for sharing a compiled database across threads
//! or storing a scanner alongside its database in a struct.

use foreign_types::ForeignType;
use std::ffi::c_void;
use std::sync::Arc;
use vectorscan_rs_sys as hs;

use super::native::{on_match_trampoline, Context, Scan};
use super::{wrapper, AsResult, Error, HyperscanErrorCode, Pattern, ScanMode};

/// A block-mode database whose compiled data is reference-counted with [`Arc`]
/// and can be shared across threads or stored in structs avoiding self-referential problems.
#[derive(Clone, Debug)]
pub struct SharedBlockDatabase {
    inner: Arc<wrapper::Database>,
}

impl SharedBlockDatabase {
    /// Create a new database with the given patterns
    pub fn new(patterns: Vec<Pattern>) -> Result<Self, Error> {
        let inner = wrapper::Database::new(patterns, ScanMode::BLOCK)?;
        Ok(Self {
            inner: Arc::new(inner),
        })
    }

    /// Create a new scanner from this database
    pub fn create_scanner(&self) -> Result<OwnedBlockScanner, Error> {
        OwnedBlockScanner::new(self)
    }

    /// Get the size in bytes of the database
    pub fn size(&self) -> Result<usize, Error> {
        self.inner.size()
    }
}

/// A block-mode scanner that owns its scratch space and an [`Arc`] reference to the database.
///
/// Unlike [`BlockScanner`](super::BlockScanner), this type has no lifetime parameter,
/// making it easy to store in structs or move between threads.
#[derive(Clone, Debug)]
pub struct OwnedBlockScanner {
    scratch: wrapper::Scratch,
    db: Arc<wrapper::Database>,
}

impl OwnedBlockScanner {
    /// Create a new scanner with the given database
    pub fn new(db: &SharedBlockDatabase) -> Result<Self, Error> {
        Ok(Self {
            scratch: wrapper::Scratch::new(&db.inner)?,
            db: Arc::clone(&db.inner),
        })
    }

    /// Scan the input using the given callback function
    ///
    /// See [`BlockScanner::scan`](super::BlockScanner::scan) for callback details.
    pub fn scan<F>(&mut self, data: &[u8], on_match: F) -> Result<Scan, Error>
    where
        F: FnMut(u32, u64, u64, u32) -> Scan,
    {
        let mut context = Context { on_match };

        let res = unsafe {
            hs::hs_scan(
                self.db.as_ptr(),
                data.as_ptr() as *const _,
                data.len() as u32,
                0,
                self.scratch.as_ptr(),
                Some(on_match_trampoline::<F>),
                &mut context as *mut _ as *mut c_void,
            )
            .ok()
        };

        match res {
            Ok(_) => Ok(Scan::Continue),
            Err(err) => match err {
                Error::Hyperscan(HyperscanErrorCode::ScanTerminated, _) => Ok(Scan::Terminate),
                err => Err(err),
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Flag, Pattern, Scan};

    #[test]
    fn shared_block_scanning_basic() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"hello".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        let mut scanner = db.create_scanner()?;

        let mut matches = Vec::new();
        scanner.scan(b"hello hello", |id, from, to, flags| {
            matches.push((id, from, to, flags));
            Scan::Continue
        })?;
        assert_eq!(matches.as_slice(), &[(0, 0, 5, 0), (0, 0, 11, 0)]);

        Ok(())
    }

    #[test]
    fn scanner_outlives_database() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"hello".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        let mut scanner = db.create_scanner()?;
        drop(db);

        let mut matches = Vec::new();
        scanner.scan(b"hello world", |id, from, to, flags| {
            matches.push((id, from, to, flags));
            Scan::Continue
        })?;
        assert_eq!(matches.len(), 1);

        Ok(())
    }

    #[test]
    fn clone_scanner() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"hello".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        let scanner = db.create_scanner()?;

        let mut scanner2 = scanner.clone();
        drop(scanner);

        let mut matches = Vec::new();
        scanner2.scan(b"hello", |id, from, to, flags| {
            matches.push((id, from, to, flags));
            Scan::Continue
        })?;
        assert_eq!(matches.len(), 1);

        Ok(())
    }

    #[test]
    fn clone_database() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"hello".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        let db2 = db.clone();
        drop(db);

        let mut scanner = db2.create_scanner()?;
        let mut matches = Vec::new();
        scanner.scan(b"hello", |id, from, to, flags| {
            matches.push((id, from, to, flags));
            Scan::Continue
        })?;
        assert_eq!(matches.len(), 1);

        Ok(())
    }

    #[test]
    fn early_termination() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"test".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        let mut scanner = db.create_scanner()?;

        let mut match_count = 0;
        scanner.scan(b"test test test", |_id, _from, _to, _flags| {
            match_count += 1;
            if match_count >= 2 {
                Scan::Terminate
            } else {
                Scan::Continue
            }
        })?;
        assert_eq!(match_count, 2);

        Ok(())
    }

    #[test]
    fn database_size() -> Result<(), Error> {
        let patterns = vec![Pattern::new(b"hello".to_vec(), Flag::default(), None)];
        let db = SharedBlockDatabase::new(patterns)?;
        assert_eq!(db.size()?, 1000); // N.B. this value may change if the vectorscan code changes
        Ok(())
    }
}
