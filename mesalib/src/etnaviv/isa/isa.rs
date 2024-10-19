// Copyright © 2024 Igalia S.L.
// SPDX-License-Identifier: MIT

use indexmap::IndexMap;
use roxmltree::Document;
use std::collections::HashMap;

/// A structure representing a bitset.
#[derive(Debug)]
pub struct Bitset<'a> {
    pub name: &'a str,
    pub displayname: Option<&'a str>,
    pub extends: Option<&'a str>,
    pub meta: HashMap<&'a str, &'a str>,
}

/// A structure representing a value in a bitset enum.
#[derive(Debug)]
pub struct BitSetEnumValue<'a> {
    pub display: &'a str,
    pub name: Option<&'a str>,
}

/// A structure representing a bitset enum.
#[derive(Debug)]
pub struct BitSetEnum<'a> {
    pub name: &'a str,
    pub values: Vec<BitSetEnumValue<'a>>,
}

/// A structure representing a bitset template.
#[derive(Debug)]
pub struct BitsetTemplate<'a> {
    pub display: &'a str,
}

/// A structure representing an Instruction Set Architecture (ISA),
/// containing bitsets and enums.
pub struct ISA<'a> {
    pub bitsets: IndexMap<&'a str, Bitset<'a>>,
    pub enums: IndexMap<&'a str, BitSetEnum<'a>>,
    pub templates: IndexMap<&'a str, BitsetTemplate<'a>>,
}

impl<'a> ISA<'a> {
    /// Creates a new `ISA` by loading bitsets and enums from a parsed XML document.
    pub fn new(doc: &'a Document) -> Self {
        let mut isa = ISA {
            bitsets: IndexMap::new(),
            enums: IndexMap::new(),
            templates: IndexMap::new(),
        };

        isa.load_from_document(doc);
        isa
    }

    /// Collects metadata for a given bitset by walking the `extends` chain in reverse order.
    pub fn collect_meta(&self, name: &'a str) -> HashMap<&'a str, &'a str> {
        let mut meta = HashMap::new();
        let mut chain = Vec::new();
        let mut current = Some(name);

        // Gather the chain of bitsets
        while let Some(item) = current {
            if let Some(bitset) = self.bitsets.get(item) {
                chain.push(bitset);
                current = bitset.extends;
            } else {
                current = None;
            }
        }

        // Collect metadata in reverse order so children's metadata overwrites their parent's.
        for bitset in chain.into_iter().rev() {
            meta.extend(&bitset.meta);
        }

        meta
    }

    /// Loads bitsets and enums from a parsed XML document into the `ISA`.
    fn load_from_document(&mut self, doc: &'a Document) {
        doc.descendants()
            .filter(|node| node.is_element() && node.has_tag_name("template"))
            .for_each(|value| {
                let name = value.attribute("name").unwrap();
                let display = value.text().unwrap();

                self.templates.insert(name, BitsetTemplate { display });
            });

        doc.descendants()
            .filter(|node| node.is_element() && node.has_tag_name("enum"))
            .for_each(|node| {
                let values = node
                    .children()
                    .filter(|node| node.is_element() && node.has_tag_name("value"))
                    .map(|value| {
                        let display = value.attribute("display").unwrap();
                        let name = value.attribute("name");

                        BitSetEnumValue { display, name }
                    })
                    .collect();

                let name = node.attribute("name").unwrap();

                self.enums.insert(name, BitSetEnum { name, values });
            });

        doc.descendants()
            .filter(|node| node.is_element() && node.has_tag_name("bitset"))
            .for_each(|node| {
                let name = node.attribute("name").unwrap();
                let displayname = node.attribute("displayname");
                let extends = node.attribute("extends");
                let meta_nodes = node
                    .children()
                    .filter(|child| child.is_element() && child.has_tag_name("meta"));

                // We can have multiple <meta> tags, which we need to combine.
                let combined_meta: HashMap<_, _> = meta_nodes
                    .flat_map(|m| m.attributes())
                    .map(|attr| (attr.name(), attr.value()))
                    .collect();

                self.bitsets.insert(
                    name,
                    Bitset {
                        name,
                        displayname,
                        extends,
                        meta: combined_meta,
                    },
                );
            });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_collect_meta() {
        let mut isa = ISA {
            bitsets: IndexMap::new(),
            enums: IndexMap::new(),
            templates: IndexMap::new(),
        };
        isa.bitsets.insert(
            "bitset1",
            Bitset {
                name: "bitset1",
                extends: None,
                meta: HashMap::from([("key1", "value1")]),
            },
        );
        isa.bitsets.insert(
            "bitset2",
            Bitset {
                name: "bitset2",
                extends: Some("bitset1"),
                meta: HashMap::from([("key2", "value2")]),
            },
        );
        isa.bitsets.insert(
            "bitset3",
            Bitset {
                name: "bitset3",
                extends: Some("bitset2"),
                meta: HashMap::from([("key3", "value3")]),
            },
        );

        let meta = isa.collect_meta("bitset3");
        assert_eq!(meta.get("key1"), Some(&"value1"));
        assert_eq!(meta.get("key2"), Some(&"value2"));
        assert_eq!(meta.get("key3"), Some(&"value3"));
    }

    #[test]
    fn test_load_from_document() {
        let xml_data = r#"
        <isa>
            <bitset name="bitset1">
                <meta key1="value1"/>
                <meta key2="value2"/>
            </bitset>
            <bitset name="bitset2" extends="bitset1"/>
            <enum name="enum1">
                <value display="val1" val="0"/>
                <value display="val2" val="1"/>
            </enum>
        </isa>
        "#;

        let doc = Document::parse(xml_data).unwrap();
        let isa = ISA::new(&doc);

        let bitset1 = isa.bitsets.get(&"bitset1").unwrap();
        assert_eq!(bitset1.name, "bitset1");
        assert_eq!(bitset1.meta.get("key1"), Some(&"value1"));
        assert_eq!(bitset1.meta.get("key2"), Some(&"value2"));

        let bitset2 = isa.bitsets.get(&"bitset2").unwrap();
        assert_eq!(bitset2.name, "bitset2");
        assert_eq!(bitset2.extends, Some("bitset1"));

        let enum1 = isa.enums.get(&"enum1").unwrap();
        assert_eq!(enum1.name, "enum1");
        assert_eq!(enum1.values.len(), 2);
        assert_eq!(enum1.values[0].display, "val1");
        assert_eq!(enum1.values[1].display, "val2");
    }
}
