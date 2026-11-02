use veyra::{
    NodeTree, PropertySet, PropertyValue, Theme, ThemeVariant, abi_version, color, layer, motion,
    property, radius, space, style_layer,
};

#[test]
fn abi_version_is_exposed() {
    assert_eq!(abi_version(), 1);
}

#[test]
fn headless_theme_property_and_tree() {
    let mut theme = Theme::new(ThemeVariant::Dark).expect("dark theme");
    theme
        .set_color(color::ACTION_PRIMARY, [10, 20, 30, 255], layer::OVERRIDE)
        .expect("theme colour");
    assert_eq!(
        theme.color(color::ACTION_PRIMARY).expect("read colour"),
        [10, 20, 30, 255]
    );

    theme
        .set_space(space::LARGE, 22.0, layer::OVERRIDE)
        .expect("theme spacing");
    assert_eq!(theme.space(space::LARGE).expect("read spacing"), 22.0);
    theme
        .set_radius(radius::SMALL, 3.0, layer::OVERRIDE)
        .expect("theme radius");
    theme
        .set_motion(motion::FAST, 80, layer::OVERRIDE)
        .expect("theme motion");
    assert_eq!(theme.motion(motion::FAST).expect("read motion"), 80);

    theme
        .apply("schema: 1\ncolor_surface = #102030\n")
        .expect("theme file");
    assert_eq!(
        theme.color(color::SURFACE).expect("surface colour"),
        [16, 32, 48, 255]
    );

    let mut properties = PropertySet::new().expect("property set");
    properties.apply_theme(&theme).expect("apply theme");
    let background = properties.resolve(property::BACKGROUND).expect("resolve");
    assert!(background.set);
    assert_eq!(background.layer, style_layer::THEME);
    assert_eq!(background.value, PropertyValue::Color([16, 32, 48, 255]));
    assert!(background.token.is_some());

    properties
        .set_color(property::FOREGROUND, style_layer::LOCAL, [200, 10, 20, 255])
        .expect("local override");
    let foreground = properties.resolve(property::FOREGROUND).expect("resolve");
    assert_eq!(foreground.layer, style_layer::LOCAL);
    assert!(foreground.token.is_none());
    assert!(
        properties
            .set_length(property::FOREGROUND, style_layer::LOCAL, 4.0)
            .is_err()
    );

    let mut tree = NodeTree::new().expect("node tree");
    tree.reserve(8).expect("reserve");
    let root = tree.create_node().expect("root");
    let child = tree.create_node().expect("child");
    tree.append(root, child).expect("append");
    assert_eq!(tree.len(), 2);
    assert_eq!(tree.parent(child).expect("parent"), Some(root));
    assert_eq!(tree.parent(root).expect("root parent"), None);
    assert!(tree.append(root, root).is_err());
}

#[test]
fn theme_file_errors_carry_location() {
    let mut theme = Theme::light().expect("light theme");
    let error = theme
        .apply("schema: 1\ncolor_missing = #ffffff\n")
        .expect_err("unknown token");
    assert_eq!(error.line, 2);
    assert_eq!(error.column, 1);
}
