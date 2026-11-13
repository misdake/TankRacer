//! Interned authored names. Forward references retain a stable symbol handle;
//! entity and class bindings are separate and entities may be rebound on reload.
use crate::{dispatch::ClassId, object_query::EntityId};
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SymbolId(usize);
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    NonAscii,
    MissingSymbol(SymbolId),
}
#[derive(Debug, Clone)]
struct Symbol {
    name: String,
    entity: Option<EntityId>,
    class: Option<ClassId>,
}
#[derive(Debug, Clone)]
pub struct Symbols {
    entries: Vec<Symbol>,
    short_names: HashMap<String, SymbolId>,
}
impl Default for Symbols {
    fn default() -> Self {
        Self {
            entries: vec![Symbol {
                name: "nil".into(),
                entity: None,
                class: None,
            }],
            short_names: HashMap::from([("nil".into(), SymbolId(0))]),
        }
    }
}
impl Symbols {
    pub fn intern(&mut self, name: &str) -> Result<SymbolId, Error> {
        let name = name.split('\0').next().unwrap_or_default();
        if !name.is_ascii() {
            return Err(Error::NonAscii);
        }
        // Native equality checks at most 32 characters, then requires BOTH
        // strings to end. Longer names are fresh symbols even on exact repeats.
        let short = name.len() <= 32;
        let key = name.to_ascii_lowercase();
        if short && let Some(&id) = self.short_names.get(&key) {
            return Ok(id);
        }
        let id = SymbolId(self.entries.len());
        self.entries.push(Symbol {
            name: name.into(),
            entity: None,
            class: None,
        });
        if short {
            self.short_names.insert(key, id);
        }
        Ok(id)
    }
    pub fn anonymous(&self, id: SymbolId) -> bool {
        id == SymbolId(0)
    }
    pub fn name(&self, id: SymbolId) -> Option<&str> {
        self.entries.get(id.0).map(|v| v.name.as_str())
    }
    pub fn entity(&self, id: SymbolId) -> Option<EntityId> {
        self.entries.get(id.0).and_then(|v| v.entity)
    }
    pub fn class(&self, id: SymbolId) -> Option<ClassId> {
        self.entries.get(id.0).and_then(|v| v.class)
    }
    /// World definitions leave the anonymous nil symbol unbound. The world
    /// factory, rather than the symbol table, checks duplicate/reserved records.
    pub fn bind_entity(&mut self, id: SymbolId, entity: EntityId) -> Result<(), Error> {
        if self.anonymous(id) {
            return Ok(());
        }
        self.entries
            .get_mut(id.0)
            .ok_or(Error::MissingSymbol(id))?
            .entity = Some(entity);
        Ok(())
    }
    pub fn bind_class(&mut self, id: SymbolId, class: ClassId) -> Result<(), Error> {
        self.entries
            .get_mut(id.0)
            .ok_or(Error::MissingSymbol(id))?
            .class = Some(class);
        Ok(())
    }
    /// Session teardown preserves interned names and class descriptors.
    pub fn clear_entities(&mut self) {
        for entry in &mut self.entries {
            entry.entity = None;
        }
    }
}

/// Direct bindings support created objects; authored attachments keep symbols
/// live instead of snapshotting their targets during level construction.
#[derive(Debug, Clone, Copy)]
pub enum Binding {
    Direct(Option<EntityId>),
    Named(SymbolId),
}
impl Binding {
    pub fn resolve(self, symbols: &Symbols) -> Option<EntityId> {
        match self {
            Self::Direct(id) => id,
            Self::Named(id) => symbols.entity(id),
        }
    }
}
impl From<Option<EntityId>> for Binding {
    fn from(id: Option<EntityId>) -> Self {
        Self::Direct(id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn forward_references_and_reload_preserve_handles_and_class_namespace() {
        let mut symbols = Symbols::default();
        let reference = symbols.intern("RoadSign").unwrap();
        let binding = Binding::Named(reference);
        assert_eq!(binding.resolve(&symbols), None);
        assert_eq!(symbols.intern("rOADsIGN").unwrap(), reference);
        symbols.bind_class(reference, ClassId(7)).unwrap();
        symbols.bind_entity(reference, EntityId(12)).unwrap();
        assert_eq!(binding.resolve(&symbols), Some(EntityId(12)));
        symbols.clear_entities();
        assert_eq!(binding.resolve(&symbols), None);
        assert_eq!(symbols.class(reference), Some(ClassId(7)));
        assert_eq!(symbols.intern("RoadSign").unwrap(), reference);
        symbols.bind_entity(reference, EntityId(3)).unwrap();
        assert_eq!(binding.resolve(&symbols), Some(EntityId(3)));
        let nil = symbols.intern("NIL").unwrap();
        symbols.bind_entity(nil, EntityId(99)).unwrap();
        assert_eq!(symbols.entity(nil), None);
        let longest = "x".repeat(32);
        assert_eq!(symbols.intern(&longest), symbols.intern(&longest));
        let longer = "x".repeat(33);
        assert_ne!(symbols.intern(&longer), symbols.intern(&longer));
    }
}
