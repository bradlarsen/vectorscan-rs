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

        {
            let mut matches = Vec::new();
            scanner.scan(b"hello hello", |id: u32, from: u64, to: u64, flags: u32| {
                matches.push((id, from, to, flags));
                Scan::Continue
            })?;
            assert_eq!(matches.as_slice(), &[(0, 0, 5, 0), (0, 0, 11, 0)]);
        }

        {
            let mut matches = Vec::new();
            scanner.scan(b"nothing to see here", |id: u32, from: u64, to: u64, flags: u32| {
                matches.push((id, from, to, flags));
                Scan::Continue
            })?;
            assert_eq!(matches.as_slice(), &[]);
        }

        Ok(())
    }
}
