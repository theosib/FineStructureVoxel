#include <gtest/gtest.h>
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"
#include "finevox/core/item_match.hpp"
#include "finevox/core/string_interner.hpp"

using namespace finevox;

// ============================================================================
// TagId Tests
// ============================================================================

class TagIdTest : public ::testing::Test {};

TEST_F(TagIdTest, DefaultIsEmpty) {
    TagId tag;
    EXPECT_TRUE(tag.isEmpty());
    EXPECT_EQ(tag.id, 0u);
}

TEST_F(TagIdTest, EmptyTagConstant) {
    EXPECT_TRUE(EMPTY_TAG.isEmpty());
    EXPECT_EQ(EMPTY_TAG.id, 0u);
}

TEST_F(TagIdTest, FromName) {
    auto tag = TagId::fromName("c:ingots/iron");
    EXPECT_FALSE(tag.isEmpty());
    EXPECT_EQ(tag.name(), "c:ingots/iron");
}

TEST_F(TagIdTest, SameNameSameId) {
    auto a = TagId::fromName("c:planks");
    auto b = TagId::fromName("c:planks");
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.id, b.id);
}

TEST_F(TagIdTest, DifferentNameDifferentId) {
    auto a = TagId::fromName("c:ingots");
    auto b = TagId::fromName("c:planks");
    EXPECT_NE(a, b);
}

TEST_F(TagIdTest, Hashable) {
    std::unordered_set<TagId> set;
    set.insert(TagId::fromName("c:ingots"));
    set.insert(TagId::fromName("c:planks"));
    set.insert(TagId::fromName("c:ingots"));  // duplicate
    EXPECT_EQ(set.size(), 2u);
}

TEST_F(TagIdTest, Comparable) {
    auto a = TagId::fromName("alpha");
    auto b = TagId::fromName("beta");
    // Just test that comparison works (ordering is by InternedId)
    EXPECT_TRUE((a < b) || (b < a) || (a == b));
}

// ============================================================================
// TagRegistry Tests
// ============================================================================

class TagRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        tags_.clear();
    }

    TagRegistry& tags_ = TagRegistry::global();
};

TEST_F(TagRegistryTest, EmptyAfterClear) {
    EXPECT_EQ(tags_.tagCount(), 0u);
    EXPECT_TRUE(tags_.allTags().empty());
}

TEST_F(TagRegistryTest, AddMemberAndQuery) {
    auto tag = TagId::fromName("c:ingots/iron");
    auto ironIngot = StringInterner::global().intern("iron_ingot");

    tags_.addMember(tag, ironIngot);
    EXPECT_TRUE(tags_.rebuild());
    EXPECT_EQ(tags_.tagCount(), 1u);

    EXPECT_TRUE(tags_.hasTag(ironIngot, tag));
    EXPECT_FALSE(tags_.hasTag(ironIngot, TagId::fromName("c:planks")));
}

TEST_F(TagRegistryTest, AddMemberByItemTypeId) {
    auto tag = TagId::fromName("c:ingots/iron");
    auto item = ItemTypeId::fromName("iron_ingot");

    tags_.addMember(tag, item);
    EXPECT_TRUE(tags_.rebuild());

    EXPECT_TRUE(tags_.hasTag(item, tag));
}

TEST_F(TagRegistryTest, AddMemberByBlockTypeId) {
    auto tag = TagId::fromName("c:ores");
    auto block = BlockTypeId::fromName("iron_ore");

    tags_.addMember(tag, block);
    EXPECT_TRUE(tags_.rebuild());

    EXPECT_TRUE(tags_.hasTag(block, tag));
}

TEST_F(TagRegistryTest, GetTagsForMember) {
    auto t1 = TagId::fromName("c:ingots");
    auto t2 = TagId::fromName("c:metals");
    auto item = StringInterner::global().intern("iron_ingot");

    tags_.addMember(t1, item);
    tags_.addMember(t2, item);
    EXPECT_TRUE(tags_.rebuild());

    auto memberTags = tags_.getTagsFor(item);
    EXPECT_EQ(memberTags.size(), 2u);

    std::unordered_set<TagId> tagSet(memberTags.begin(), memberTags.end());
    EXPECT_TRUE(tagSet.contains(t1));
    EXPECT_TRUE(tagSet.contains(t2));
}

TEST_F(TagRegistryTest, GetMembersOfTag) {
    auto tag = TagId::fromName("c:ingots/iron");
    auto a = StringInterner::global().intern("iron_ingot");
    auto b = StringInterner::global().intern("modA:iron_ingot");

    tags_.addMember(tag, a);
    tags_.addMember(tag, b);
    EXPECT_TRUE(tags_.rebuild());

    auto members = tags_.getMembersOf(tag);
    EXPECT_EQ(members.size(), 2u);

    std::unordered_set<InternedId> memberSet(members.begin(), members.end());
    EXPECT_TRUE(memberSet.contains(a));
    EXPECT_TRUE(memberSet.contains(b));
}

TEST_F(TagRegistryTest, SimpleComposition) {
    auto parent = TagId::fromName("c:ingots");
    auto child = TagId::fromName("c:ingots/iron");

    auto ironIngot = StringInterner::global().intern("iron_ingot");
    tags_.addMember(child, ironIngot);
    tags_.addInclude(parent, child);

    EXPECT_TRUE(tags_.rebuild());

    // ironIngot should be in both tags
    EXPECT_TRUE(tags_.hasTag(ironIngot, child));
    EXPECT_TRUE(tags_.hasTag(ironIngot, parent));

    auto parentMembers = tags_.getMembersOf(parent);
    EXPECT_EQ(parentMembers.size(), 1u);
    EXPECT_EQ(parentMembers[0], ironIngot);
}

TEST_F(TagRegistryTest, TransitiveComposition) {
    auto top = TagId::fromName("c:metals");
    auto mid = TagId::fromName("c:ingots");
    auto leaf = TagId::fromName("c:ingots/iron");

    auto ironIngot = StringInterner::global().intern("iron_ingot");
    tags_.addMember(leaf, ironIngot);
    tags_.addInclude(mid, leaf);
    tags_.addInclude(top, mid);

    EXPECT_TRUE(tags_.rebuild());

    // ironIngot should be in all three
    EXPECT_TRUE(tags_.hasTag(ironIngot, leaf));
    EXPECT_TRUE(tags_.hasTag(ironIngot, mid));
    EXPECT_TRUE(tags_.hasTag(ironIngot, top));
}

TEST_F(TagRegistryTest, DiamondComposition) {
    // top includes both left and right, both include leaf
    auto top = TagId::fromName("top");
    auto left = TagId::fromName("left");
    auto right = TagId::fromName("right");
    auto leaf = TagId::fromName("leaf");

    auto item = StringInterner::global().intern("item");
    tags_.addMember(leaf, item);
    tags_.addInclude(left, leaf);
    tags_.addInclude(right, leaf);
    tags_.addInclude(top, left);
    tags_.addInclude(top, right);

    EXPECT_TRUE(tags_.rebuild());

    EXPECT_TRUE(tags_.hasTag(item, top));
    auto topMembers = tags_.getMembersOf(top);
    EXPECT_EQ(topMembers.size(), 1u);
}

TEST_F(TagRegistryTest, CycleDetection) {
    auto a = TagId::fromName("tag_a");
    auto b = TagId::fromName("tag_b");

    auto item = StringInterner::global().intern("item");
    tags_.addMember(a, item);
    tags_.addInclude(a, b);
    tags_.addInclude(b, a);  // cycle!

    // rebuild() should return false (cycle detected)
    EXPECT_FALSE(tags_.rebuild());

    // Despite cycle, the item should still be reachable
    EXPECT_TRUE(tags_.hasTag(item, a));
}

TEST_F(TagRegistryTest, SelfCycle) {
    auto a = TagId::fromName("tag_a");
    auto item = StringInterner::global().intern("item");

    tags_.addMember(a, item);
    tags_.addInclude(a, a);  // self-cycle

    EXPECT_FALSE(tags_.rebuild());
    EXPECT_TRUE(tags_.hasTag(item, a));
}

TEST_F(TagRegistryTest, MultipleRebuild) {
    auto tag = TagId::fromName("c:ingots");
    auto item = StringInterner::global().intern("iron_ingot");

    tags_.addMember(tag, item);
    EXPECT_TRUE(tags_.rebuild());
    EXPECT_TRUE(tags_.isResolved());

    // Add more data — invalidates resolved
    auto item2 = StringInterner::global().intern("copper_ingot");
    tags_.addMember(tag, item2);
    EXPECT_FALSE(tags_.isResolved());

    // Rebuild again
    EXPECT_TRUE(tags_.rebuild());
    EXPECT_TRUE(tags_.isResolved());

    auto members = tags_.getMembersOf(tag);
    EXPECT_EQ(members.size(), 2u);
}

TEST_F(TagRegistryTest, CompositionMergesDirectAndIncluded) {
    auto parent = TagId::fromName("c:ingots");
    auto child = TagId::fromName("c:ingots/iron");

    auto directItem = StringInterner::global().intern("gold_ingot");
    auto childItem = StringInterner::global().intern("iron_ingot");

    tags_.addMember(parent, directItem);
    tags_.addMember(child, childItem);
    tags_.addInclude(parent, child);

    EXPECT_TRUE(tags_.rebuild());

    auto parentMembers = tags_.getMembersOf(parent);
    EXPECT_EQ(parentMembers.size(), 2u);

    std::unordered_set<InternedId> memberSet(parentMembers.begin(), parentMembers.end());
    EXPECT_TRUE(memberSet.contains(directItem));
    EXPECT_TRUE(memberSet.contains(childItem));
}

TEST_F(TagRegistryTest, UnknownTagQuery) {
    EXPECT_TRUE(tags_.rebuild());
    auto bogusTag = TagId::fromName("nonexistent");
    auto bogusItem = StringInterner::global().intern("nothing");

    EXPECT_FALSE(tags_.hasTag(bogusItem, bogusTag));
    EXPECT_TRUE(tags_.getTagsFor(bogusItem).empty());
    EXPECT_TRUE(tags_.getMembersOf(bogusTag).empty());
}

// ============================================================================
// .tag File Loading Tests
// ============================================================================

class TagFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        tags_.clear();
        unify_.clear();
    }

    TagRegistry& tags_ = TagRegistry::global();
    UnificationRegistry& unify_ = UnificationRegistry::global();
};

TEST_F(TagFileTest, SimpleTagBlock) {
    auto content = R"(
tag c:ingots/iron {
    iron_ingot
    modA:iron_ingot
}
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(tags_.rebuild());

    auto tag = TagId::fromName("c:ingots/iron");
    auto members = tags_.getMembersOf(tag);
    EXPECT_EQ(members.size(), 2u);
}

TEST_F(TagFileTest, TagWithIncludes) {
    auto content = R"(
tag c:ingots/iron {
    iron_ingot
}

tag c:ingots/copper {
    copper_ingot
}

tag c:ingots {
    include c:ingots/iron
    include c:ingots/copper
}
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 3);

    EXPECT_TRUE(tags_.rebuild());

    auto parentTag = TagId::fromName("c:ingots");
    auto members = tags_.getMembersOf(parentTag);
    EXPECT_EQ(members.size(), 2u);
}

TEST_F(TagFileTest, CommentsAndBlankLines) {
    auto content = R"(
# This is a comment

tag c:planks {
    # Also a comment
    oak_planks

    birch_planks
}
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(tags_.rebuild());

    auto tag = TagId::fromName("c:planks");
    auto members = tags_.getMembersOf(tag);
    EXPECT_EQ(members.size(), 2u);
}

TEST_F(TagFileTest, UnifyBlock) {
    auto content = R"(
unify nickel {
    canonical: nickel_ingot
    members: nickel_ingot, modA:nickel_ingot, modB:nickel_ingot
    auto_convert: true
}
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 1);

    auto canonical = ItemTypeId::fromName("nickel_ingot");
    auto modA = ItemTypeId::fromName("modA:nickel_ingot");
    auto modB = ItemTypeId::fromName("modB:nickel_ingot");

    EXPECT_EQ(unify_.resolve(modA), canonical);
    EXPECT_EQ(unify_.resolve(modB), canonical);
    EXPECT_TRUE(unify_.areEquivalent(modA, modB));
    EXPECT_TRUE(unify_.isAutoConvert(modA));
}

TEST_F(TagFileTest, SeparateDirective) {
    auto content = R"(
separate modA:redstone, modB:redstone
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 1);
    // The separate declaration means these items won't be auto-unified.
    // We verify this works correctly via the auto-resolve test below.
}

TEST_F(TagFileTest, EmptyContent) {
    int count = loadTagFileFromString("", tags_, unify_);
    EXPECT_EQ(count, 0);
}

TEST_F(TagFileTest, MissingTagName) {
    auto content = "tag {\n}\n";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, -1);
}

TEST_F(TagFileTest, UnclosedBlock) {
    auto content = "tag c:stuff {\n    item1\n";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, -1);
}

TEST_F(TagFileTest, MultipleBlocks) {
    auto content = R"(
tag c:ingots/iron {
    iron_ingot
}

tag c:ingots/copper {
    copper_ingot
}

tag c:ingots {
    include c:ingots/iron
    include c:ingots/copper
}

unify iron_ingot {
    canonical: iron_ingot
    members: iron_ingot, modA:iron_ingot
}

separate foo, bar
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 5);
}

// ============================================================================
// UnificationRegistry Tests
// ============================================================================

class UnificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        tags_.clear();
        unify_.clear();
    }

    TagRegistry& tags_ = TagRegistry::global();
    UnificationRegistry& unify_ = UnificationRegistry::global();
};

TEST_F(UnificationTest, EmptyAfterClear) {
    EXPECT_EQ(unify_.groupCount(), 0u);
}

TEST_F(UnificationTest, DeclareGroup) {
    auto canonical = ItemTypeId::fromName("nickel_ingot");
    auto modA = ItemTypeId::fromName("modA:nickel_ingot");
    auto modB = ItemTypeId::fromName("modB:nickel_ingot");

    unify_.declareGroup(canonical, {canonical, modA, modB}, true);

    EXPECT_EQ(unify_.groupCount(), 1u);
    EXPECT_EQ(unify_.resolve(modA), canonical);
    EXPECT_EQ(unify_.resolve(modB), canonical);
    EXPECT_EQ(unify_.resolve(canonical), canonical);
    EXPECT_TRUE(unify_.areEquivalent(modA, modB));
    EXPECT_TRUE(unify_.areEquivalent(canonical, modB));
}

TEST_F(UnificationTest, ResolveNonUnified) {
    auto item = ItemTypeId::fromName("unique_item");
    EXPECT_EQ(unify_.resolve(item), item);
}

TEST_F(UnificationTest, AreEquivalentNonUnified) {
    auto a = ItemTypeId::fromName("item_a");
    auto b = ItemTypeId::fromName("item_b");
    EXPECT_FALSE(unify_.areEquivalent(a, b));
}

TEST_F(UnificationTest, SelfEquivalent) {
    auto a = ItemTypeId::fromName("item_a");
    EXPECT_TRUE(unify_.areEquivalent(a, a));
}

TEST_F(UnificationTest, GetGroup) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA});

    auto group = unify_.getGroup(modA);
    EXPECT_EQ(group.size(), 2u);
}

TEST_F(UnificationTest, GetGroupNonUnified) {
    auto item = ItemTypeId::fromName("unique_item");
    auto group = unify_.getGroup(item);
    EXPECT_TRUE(group.empty());
}

TEST_F(UnificationTest, GetCanonical) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA});

    EXPECT_EQ(unify_.getCanonical(modA), canonical);
    EXPECT_EQ(unify_.getCanonical(canonical), canonical);
}

TEST_F(UnificationTest, GetCanonicalNonUnified) {
    auto item = ItemTypeId::fromName("unique_item");
    EXPECT_EQ(unify_.getCanonical(item), item);
}

TEST_F(UnificationTest, AutoConvertFalse) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA}, false);

    EXPECT_FALSE(unify_.isAutoConvert(modA));
    // With autoConvert=false, resolve returns self
    EXPECT_EQ(unify_.resolve(modA), modA);
    // But they're still equivalent
    EXPECT_TRUE(unify_.areEquivalent(canonical, modA));
}

TEST_F(UnificationTest, CanonicalAutoIncluded) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    // Don't include canonical in members list — should be auto-added
    unify_.declareGroup(canonical, {modA});

    auto group = unify_.getGroup(canonical);
    EXPECT_EQ(group.size(), 2u);
    EXPECT_TRUE(unify_.areEquivalent(canonical, modA));
}

TEST_F(UnificationTest, DuplicateGroupWarning) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA});
    // Declaring again should be ignored
    unify_.declareGroup(canonical, {canonical, modA});

    EXPECT_EQ(unify_.groupCount(), 1u);
}

TEST_F(UnificationTest, DeclareSeparate) {
    auto a = ItemTypeId::fromName("modA:redstone");
    auto b = ItemTypeId::fromName("modB:redstone");

    unify_.declareSeparate({a, b});

    // These items should not be auto-unified
    // (Tested through autoResolve — manual declare still works)
    auto canonical = ItemTypeId::fromName("redstone");
    unify_.declareGroup(canonical, {canonical, a, b});

    EXPECT_EQ(unify_.groupCount(), 1u);
    EXPECT_TRUE(unify_.areEquivalent(a, b));
}

// ============================================================================
// Auto-Resolution Tests
// ============================================================================

TEST_F(UnificationTest, AutoResolveBySharedTag) {
    // Set up items from different namespaces with same community tag
    auto tag = TagId::fromName("c:ingots/nickel");
    auto plain = ItemTypeId::fromName("nickel_ingot");
    auto modA = ItemTypeId::fromName("modA:nickel_ingot");
    auto modB = ItemTypeId::fromName("modB:nickel_ingot");

    tags_.addMember(tag, plain);
    tags_.addMember(tag, modA);
    tags_.addMember(tag, modB);
    tags_.rebuild();

    unify_.autoResolve(tags_);

    EXPECT_GE(unify_.groupCount(), 1u);
    EXPECT_TRUE(unify_.areEquivalent(plain, modA));
    EXPECT_TRUE(unify_.areEquivalent(plain, modB));

    // Canonical should be the unnamespaced one
    EXPECT_EQ(unify_.getCanonical(modA), plain);
}

TEST_F(UnificationTest, AutoResolveSeparateOverride) {
    auto tag = TagId::fromName("c:dusts/redstone");
    auto modA = ItemTypeId::fromName("modA:redstone");
    auto modB = ItemTypeId::fromName("modB:redstone");

    tags_.addMember(tag, modA);
    tags_.addMember(tag, modB);
    tags_.rebuild();

    unify_.declareSeparate({modA, modB});
    unify_.autoResolve(tags_);

    EXPECT_FALSE(unify_.areEquivalent(modA, modB));
}

TEST_F(UnificationTest, TagPropagation) {
    // Two items are unified; one has extra tags
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    auto tagIngots = TagId::fromName("c:ingots/iron");
    auto tagMetals = TagId::fromName("c:metals");

    tags_.addMember(tagIngots, canonical);
    tags_.addMember(tagIngots, modA);
    tags_.addMember(tagMetals, canonical);  // Only canonical has this
    tags_.rebuild();

    unify_.declareGroup(canonical, {canonical, modA});
    unify_.propagateTags(tags_);
    tags_.rebuild();

    // modA should now also have c:metals
    EXPECT_TRUE(tags_.hasTag(modA, tagMetals));
}

TEST_F(UnificationTest, TagPropagationBidirectional) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    auto tag1 = TagId::fromName("c:ingots/iron");
    auto tag2 = TagId::fromName("modA:special_metals");

    tags_.addMember(tag1, canonical);
    tags_.addMember(tag2, modA);  // Only modA has this
    tags_.rebuild();

    unify_.declareGroup(canonical, {canonical, modA});
    unify_.propagateTags(tags_);
    tags_.rebuild();

    // canonical should get modA's tag
    EXPECT_TRUE(tags_.hasTag(canonical, tag2));
    // modA should get canonical's tag
    EXPECT_TRUE(tags_.hasTag(modA, tag1));
}

// ============================================================================
// ItemMatch Tests
// ============================================================================

class ItemMatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        tags_.clear();
        unify_.clear();
    }

    TagRegistry& tags_ = TagRegistry::global();
    UnificationRegistry& unify_ = UnificationRegistry::global();
};

TEST_F(ItemMatchTest, EmptyMatchesEmpty) {
    auto m = ItemMatch::empty();
    EXPECT_TRUE(m.isEmpty());
    EXPECT_TRUE(m.matches(ItemTypeId{}));
    EXPECT_FALSE(m.matches(ItemTypeId::fromName("iron_ingot")));
}

TEST_F(ItemMatchTest, ExactMatchesSame) {
    auto iron = ItemTypeId::fromName("iron_ingot");
    auto m = ItemMatch::exact(iron);

    EXPECT_TRUE(m.isExact());
    EXPECT_TRUE(m.matches(iron));
    EXPECT_FALSE(m.matches(ItemTypeId::fromName("copper_ingot")));
}

TEST_F(ItemMatchTest, ExactMatchesThroughUnification) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA}, true);

    // Match asks for iron_ingot — modA:iron_ingot should match (both resolve to canonical)
    auto m = ItemMatch::exact(canonical);
    EXPECT_TRUE(m.matches(modA));
    EXPECT_TRUE(m.matches(canonical));
}

TEST_F(ItemMatchTest, ExactNoMatchWhenAutoConvertOff) {
    auto canonical = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");

    unify_.declareGroup(canonical, {canonical, modA}, false);

    // With autoConvert off, resolve returns self
    auto m = ItemMatch::exact(canonical);
    EXPECT_FALSE(m.matches(modA));
    EXPECT_TRUE(m.matches(canonical));
}

TEST_F(ItemMatchTest, TaggedMatchesMember) {
    auto tag = TagId::fromName("c:ingots/iron");
    auto iron = ItemTypeId::fromName("iron_ingot");
    auto copper = ItemTypeId::fromName("copper_ingot");

    tags_.addMember(tag, iron);
    tags_.rebuild();

    auto m = ItemMatch::tagged(tag);
    EXPECT_TRUE(m.isTagged());
    EXPECT_TRUE(m.matches(iron));
    EXPECT_FALSE(m.matches(copper));
}

TEST_F(ItemMatchTest, TaggedMatchesTransitiveMembers) {
    auto parent = TagId::fromName("c:ingots");
    auto child = TagId::fromName("c:ingots/iron");

    auto iron = ItemTypeId::fromName("iron_ingot");
    tags_.addMember(child, iron);
    tags_.addInclude(parent, child);
    tags_.rebuild();

    auto m = ItemMatch::tagged(parent);
    EXPECT_TRUE(m.matches(iron));
}

TEST_F(ItemMatchTest, TaggedDoesNotMatchEmpty) {
    auto tag = TagId::fromName("c:ingots");
    auto m = ItemMatch::tagged(tag);
    EXPECT_FALSE(m.matches(ItemTypeId{}));
}

// ============================================================================
// Integration Test — Full Initialization Workflow
// ============================================================================

TEST_F(ItemMatchTest, FullInitWorkflow) {
    // Step 1: Load tag definitions
    auto content = R"(
tag c:ingots/iron {
    iron_ingot
    modA:iron_ingot
}

tag c:ingots/copper {
    copper_ingot
}

tag c:ingots {
    include c:ingots/iron
    include c:ingots/copper
}
)";
    int count = loadTagFileFromString(content, tags_, unify_);
    EXPECT_EQ(count, 3);

    // Step 2: Resolve tag composition
    EXPECT_TRUE(tags_.rebuild());

    // Step 3: Auto-resolve unification (iron_ingot + modA:iron_ingot share c:ingots/iron)
    unify_.autoResolve(tags_);

    // Step 4: Propagate tags
    unify_.propagateTags(tags_);

    // Step 5: Rebuild with propagated tags
    EXPECT_TRUE(tags_.rebuild());

    // Verify: iron_ingot and modA:iron_ingot are unified
    auto iron = ItemTypeId::fromName("iron_ingot");
    auto modA = ItemTypeId::fromName("modA:iron_ingot");
    EXPECT_TRUE(unify_.areEquivalent(iron, modA));

    // Verify: canonical is the unnamespaced one
    EXPECT_EQ(unify_.getCanonical(modA), iron);

    // Verify: tagged match for "any ingot" works
    auto ingotsTag = TagId::fromName("c:ingots");
    auto m = ItemMatch::tagged(ingotsTag);
    EXPECT_TRUE(m.matches(iron));
    EXPECT_TRUE(m.matches(modA));
    EXPECT_TRUE(m.matches(ItemTypeId::fromName("copper_ingot")));
    EXPECT_FALSE(m.matches(ItemTypeId::fromName("diamond")));

    // Verify: exact match works through unification
    auto exactIron = ItemMatch::exact(iron);
    EXPECT_TRUE(exactIron.matches(modA));
}
