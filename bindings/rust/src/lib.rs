use std::ffi::CStr;
use std::os::raw::c_char;
use std::ptr::NonNull;

#[repr(C)]
pub struct VyTheme {
    _private: [u8; 0],
}

#[repr(C)]
pub struct VyPropertySet {
    _private: [u8; 0],
}

#[repr(C)]
pub struct VyNodeTree {
    _private: [u8; 0],
}

#[repr(C)]
struct VyUtf8 {
    data: *const c_char,
    size: u64,
}

#[repr(C)]
struct VyNodeId {
    index: u32,
    generation: u32,
}

#[repr(C)]
struct VyThemeError {
    size: u32,
    version: u32,
    status: i32,
    line: u32,
    column: u32,
}

#[repr(C)]
struct VyResolvedProperty {
    size: u32,
    version: u32,
    property: u8,
    set: u8,
    value_kind: u8,
    layer: u8,
    red: u8,
    green: u8,
    blue: u8,
    alpha: u8,
    length: f32,
    duration_ms: u32,
    number: f32,
    from_token: u8,
    token_kind: u8,
    token: u8,
    line: u32,
}

unsafe extern "C" {
    fn vy_status_name(status: i32) -> *const c_char;
    fn vy_abi_version() -> u32;

    fn vy_theme_create(variant: i32, out_theme: *mut *mut VyTheme) -> i32;
    fn vy_theme_destroy(theme: *mut VyTheme);
    fn vy_theme_set_color(
        theme: *mut VyTheme,
        token: u8,
        red: u8,
        green: u8,
        blue: u8,
        alpha: u8,
        layer: u8,
    ) -> i32;
    fn vy_theme_color(theme: *const VyTheme, token: u8, out_rgba: *mut u8) -> i32;
    fn vy_theme_set_space(theme: *mut VyTheme, token: u8, value: f32, layer: u8) -> i32;
    fn vy_theme_space(theme: *const VyTheme, token: u8, out_value: *mut f32) -> i32;
    fn vy_theme_set_radius(theme: *mut VyTheme, token: u8, value: f32, layer: u8) -> i32;
    fn vy_theme_radius(theme: *const VyTheme, token: u8, out_value: *mut f32) -> i32;
    fn vy_theme_set_motion(theme: *mut VyTheme, token: u8, value: u32, layer: u8) -> i32;
    fn vy_theme_motion(theme: *const VyTheme, token: u8, out_value: *mut u32) -> i32;
    fn vy_theme_apply(theme: *mut VyTheme, text: VyUtf8, error: *mut VyThemeError) -> i32;

    fn vy_property_set_create(out_set: *mut *mut VyPropertySet) -> i32;
    fn vy_property_set_destroy(set: *mut VyPropertySet);
    fn vy_property_set_apply_theme(set: *mut VyPropertySet, theme: *const VyTheme) -> i32;
    fn vy_property_set_set_color(
        set: *mut VyPropertySet,
        property: u8,
        layer: u8,
        red: u8,
        green: u8,
        blue: u8,
        alpha: u8,
    ) -> i32;
    fn vy_property_set_set_length(
        set: *mut VyPropertySet,
        property: u8,
        layer: u8,
        value: f32,
    ) -> i32;
    fn vy_property_set_set_duration(
        set: *mut VyPropertySet,
        property: u8,
        layer: u8,
        value: u32,
    ) -> i32;
    fn vy_property_set_set_number(
        set: *mut VyPropertySet,
        property: u8,
        layer: u8,
        value: f32,
    ) -> i32;
    fn vy_property_set_resolve(
        set: *const VyPropertySet,
        property: u8,
        out: *mut VyResolvedProperty,
    ) -> i32;

    fn vy_node_tree_create(out_tree: *mut *mut VyNodeTree) -> i32;
    fn vy_node_tree_destroy(tree: *mut VyNodeTree);
    fn vy_node_tree_reserve(tree: *mut VyNodeTree, count: u64) -> i32;
    fn vy_node_create(tree: *mut VyNodeTree, out_node: *mut VyNodeId) -> i32;
    fn vy_node_append(tree: *mut VyNodeTree, parent: VyNodeId, child: VyNodeId) -> i32;
    fn vy_node_parent(tree: *const VyNodeTree, node: VyNodeId, out_parent: *mut VyNodeId) -> i32;
    fn vy_node_count(tree: *const VyNodeTree) -> u64;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThemeVariant {
    Light = 0,
    Dark = 1,
    HighContrast = 2,
}

pub mod layer {
    pub const DEFAULTS: u8 = 0;
    pub const PRESET: u8 = 1;
    pub const FILE: u8 = 2;
    pub const OVERRIDE: u8 = 3;
}

pub mod style_layer {
    pub const PLATFORM_DEFAULT: u8 = 0;
    pub const THEME: u8 = 1;
    pub const RECIPE: u8 = 2;
    pub const VARIANT: u8 = 3;
    pub const STATE: u8 = 4;
    pub const LOCAL: u8 = 5;
    pub const CONTROLLER: u8 = 6;
    pub const ACCESSIBILITY: u8 = 7;
}

pub mod color {
    pub const SURFACE: u8 = 0;
    pub const TEXT_PRIMARY: u8 = 1;
    pub const ACTION_PRIMARY: u8 = 2;
    pub const ACTION_ON_PRIMARY: u8 = 3;
    pub const ACTION_HOVERED: u8 = 4;
    pub const ACTION_PRESSED: u8 = 5;
    pub const ACTION_DISABLED: u8 = 6;
    pub const TEXT_DISABLED: u8 = 7;
    pub const TOGGLE_CHECKED: u8 = 8;
    pub const TOGGLE_ON_CHECKED: u8 = 9;
    pub const ERROR: u8 = 10;
}

pub mod space {
    pub const NONE: u8 = 0;
    pub const EXTRA_SMALL: u8 = 1;
    pub const SMALL: u8 = 2;
    pub const MEDIUM: u8 = 3;
    pub const LARGE: u8 = 4;
    pub const EXTRA_LARGE: u8 = 5;
}

pub mod radius {
    pub const NONE: u8 = 0;
    pub const SMALL: u8 = 1;
    pub const MEDIUM: u8 = 2;
    pub const LARGE: u8 = 3;
}

pub mod motion {
    pub const INSTANT: u8 = 0;
    pub const FAST: u8 = 1;
    pub const NORMAL: u8 = 2;
    pub const SLOW: u8 = 3;
}

pub mod property {
    pub const FOREGROUND: u8 = 0;
    pub const BACKGROUND: u8 = 1;
    pub const BORDER_COLOR: u8 = 2;
    pub const CORNER_RADIUS: u8 = 3;
    pub const BORDER_WIDTH: u8 = 4;
    pub const PADDING: u8 = 5;
    pub const FONT_SIZE: u8 = 6;
    pub const OPACITY: u8 = 7;
    pub const TRANSITION_DURATION: u8 = 8;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VeyraError {
    status: i32,
}

impl VeyraError {
    pub fn status(self) -> i32 {
        self.status
    }
}

impl std::fmt::Display for VeyraError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(status_name(self.status))
    }
}

impl std::error::Error for VeyraError {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ThemeFileError {
    pub status: i32,
    pub line: u32,
    pub column: u32,
}

impl std::fmt::Display for ThemeFileError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            formatter,
            "{} at {}:{}",
            status_name(self.status),
            self.line,
            self.column
        )
    }
}

impl std::error::Error for ThemeFileError {}

pub fn status_name(status: i32) -> &'static str {
    let pointer = unsafe { vy_status_name(status) };
    if pointer.is_null() {
        return "unknown";
    }
    unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .unwrap_or("unknown")
}

pub fn abi_version() -> u32 {
    unsafe { vy_abi_version() }
}

fn check(status: i32) -> Result<(), VeyraError> {
    if status == 0 {
        Ok(())
    } else {
        Err(VeyraError { status })
    }
}

pub struct Theme {
    raw: NonNull<VyTheme>,
}

impl Theme {
    pub fn new(variant: ThemeVariant) -> Result<Self, VeyraError> {
        let mut raw = std::ptr::null_mut();
        check(unsafe { vy_theme_create(variant as i32, &mut raw) })?;
        NonNull::new(raw)
            .map(|raw| Self { raw })
            .ok_or(VeyraError { status: 5 })
    }

    pub fn light() -> Result<Self, VeyraError> {
        Self::new(ThemeVariant::Light)
    }

    pub fn dark() -> Result<Self, VeyraError> {
        Self::new(ThemeVariant::Dark)
    }

    pub fn high_contrast() -> Result<Self, VeyraError> {
        Self::new(ThemeVariant::HighContrast)
    }

    pub fn set_color(&mut self, token: u8, rgba: [u8; 4], layer: u8) -> Result<(), VeyraError> {
        check(unsafe {
            vy_theme_set_color(
                self.raw.as_ptr(),
                token,
                rgba[0],
                rgba[1],
                rgba[2],
                rgba[3],
                layer,
            )
        })
    }

    pub fn color(&self, token: u8) -> Result<[u8; 4], VeyraError> {
        let mut rgba = [0u8; 4];
        check(unsafe { vy_theme_color(self.raw.as_ptr(), token, rgba.as_mut_ptr()) })?;
        Ok(rgba)
    }

    pub fn set_space(&mut self, token: u8, value: f32, layer: u8) -> Result<(), VeyraError> {
        check(unsafe { vy_theme_set_space(self.raw.as_ptr(), token, value, layer) })
    }

    pub fn space(&self, token: u8) -> Result<f32, VeyraError> {
        let mut value = 0.0;
        check(unsafe { vy_theme_space(self.raw.as_ptr(), token, &mut value) })?;
        Ok(value)
    }

    pub fn set_radius(&mut self, token: u8, value: f32, layer: u8) -> Result<(), VeyraError> {
        check(unsafe { vy_theme_set_radius(self.raw.as_ptr(), token, value, layer) })
    }

    pub fn radius(&self, token: u8) -> Result<f32, VeyraError> {
        let mut value = 0.0;
        check(unsafe { vy_theme_radius(self.raw.as_ptr(), token, &mut value) })?;
        Ok(value)
    }

    pub fn set_motion(&mut self, token: u8, value: u32, layer: u8) -> Result<(), VeyraError> {
        check(unsafe { vy_theme_set_motion(self.raw.as_ptr(), token, value, layer) })
    }

    pub fn motion(&self, token: u8) -> Result<u32, VeyraError> {
        let mut value = 0;
        check(unsafe { vy_theme_motion(self.raw.as_ptr(), token, &mut value) })?;
        Ok(value)
    }

    pub fn apply(&mut self, text: &str) -> Result<(), ThemeFileError> {
        let mut error = VyThemeError {
            size: std::mem::size_of::<VyThemeError>() as u32,
            version: 1,
            status: 0,
            line: 0,
            column: 0,
        };
        let status = unsafe {
            vy_theme_apply(
                self.raw.as_ptr(),
                VyUtf8 {
                    data: text.as_ptr() as *const c_char,
                    size: text.len() as u64,
                },
                &mut error,
            )
        };
        if status == 0 {
            Ok(())
        } else {
            Err(ThemeFileError {
                status: error.status,
                line: error.line,
                column: error.column,
            })
        }
    }
}

impl Drop for Theme {
    fn drop(&mut self) {
        unsafe { vy_theme_destroy(self.raw.as_ptr()) };
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PropertyValue {
    Color([u8; 4]),
    Length(f32),
    Duration(u32),
    Number(f32),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TokenRef {
    pub kind: u8,
    pub token: u8,
    pub line: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ResolvedProperty {
    pub property: u8,
    pub set: bool,
    pub value: PropertyValue,
    pub layer: u8,
    pub token: Option<TokenRef>,
}

pub struct PropertySet {
    raw: NonNull<VyPropertySet>,
}

impl PropertySet {
    pub fn new() -> Result<Self, VeyraError> {
        let mut raw = std::ptr::null_mut();
        check(unsafe { vy_property_set_create(&mut raw) })?;
        NonNull::new(raw)
            .map(|raw| Self { raw })
            .ok_or(VeyraError { status: 5 })
    }

    pub fn apply_theme(&mut self, theme: &Theme) -> Result<(), VeyraError> {
        check(unsafe { vy_property_set_apply_theme(self.raw.as_ptr(), theme.raw.as_ptr()) })
    }

    pub fn set_color(&mut self, property: u8, layer: u8, rgba: [u8; 4]) -> Result<(), VeyraError> {
        check(unsafe {
            vy_property_set_set_color(
                self.raw.as_ptr(),
                property,
                layer,
                rgba[0],
                rgba[1],
                rgba[2],
                rgba[3],
            )
        })
    }

    pub fn set_length(&mut self, property: u8, layer: u8, value: f32) -> Result<(), VeyraError> {
        check(unsafe { vy_property_set_set_length(self.raw.as_ptr(), property, layer, value) })
    }

    pub fn set_duration(&mut self, property: u8, layer: u8, value: u32) -> Result<(), VeyraError> {
        check(unsafe { vy_property_set_set_duration(self.raw.as_ptr(), property, layer, value) })
    }

    pub fn set_number(&mut self, property: u8, layer: u8, value: f32) -> Result<(), VeyraError> {
        check(unsafe { vy_property_set_set_number(self.raw.as_ptr(), property, layer, value) })
    }

    pub fn resolve(&self, property: u8) -> Result<ResolvedProperty, VeyraError> {
        let mut raw = VyResolvedProperty {
            size: 0,
            version: 0,
            property: 0,
            set: 0,
            value_kind: 0,
            layer: 0,
            red: 0,
            green: 0,
            blue: 0,
            alpha: 0,
            length: 0.0,
            duration_ms: 0,
            number: 0.0,
            from_token: 0,
            token_kind: 0,
            token: 0,
            line: 0,
        };
        check(unsafe { vy_property_set_resolve(self.raw.as_ptr(), property, &mut raw) })?;

        let value = match raw.value_kind {
            0 => PropertyValue::Color([raw.red, raw.green, raw.blue, raw.alpha]),
            1 => PropertyValue::Length(raw.length),
            2 => PropertyValue::Duration(raw.duration_ms),
            3 => PropertyValue::Number(raw.number),
            _ => return Err(VeyraError { status: 4 }),
        };

        Ok(ResolvedProperty {
            property: raw.property,
            set: raw.set != 0,
            value,
            layer: raw.layer,
            token: (raw.from_token != 0).then_some(TokenRef {
                kind: raw.token_kind,
                token: raw.token,
                line: raw.line,
            }),
        })
    }
}

impl Drop for PropertySet {
    fn drop(&mut self) {
        unsafe { vy_property_set_destroy(self.raw.as_ptr()) };
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NodeId {
    pub index: u32,
    pub generation: u32,
}

impl NodeId {
    fn to_ffi(self) -> VyNodeId {
        VyNodeId {
            index: self.index,
            generation: self.generation,
        }
    }

    fn from_ffi(raw: VyNodeId) -> Option<Self> {
        if raw.index == u32::MAX {
            None
        } else {
            Some(Self {
                index: raw.index,
                generation: raw.generation,
            })
        }
    }
}

pub struct NodeTree {
    raw: NonNull<VyNodeTree>,
}

impl NodeTree {
    pub fn new() -> Result<Self, VeyraError> {
        let mut raw = std::ptr::null_mut();
        check(unsafe { vy_node_tree_create(&mut raw) })?;
        NonNull::new(raw)
            .map(|raw| Self { raw })
            .ok_or(VeyraError { status: 5 })
    }

    pub fn reserve(&mut self, count: u64) -> Result<(), VeyraError> {
        check(unsafe { vy_node_tree_reserve(self.raw.as_ptr(), count) })
    }

    pub fn create_node(&mut self) -> Result<NodeId, VeyraError> {
        let mut raw = VyNodeId {
            index: u32::MAX,
            generation: 0,
        };
        check(unsafe { vy_node_create(self.raw.as_ptr(), &mut raw) })?;
        NodeId::from_ffi(raw).ok_or(VeyraError { status: 11 })
    }

    pub fn append(&mut self, parent: NodeId, child: NodeId) -> Result<(), VeyraError> {
        check(unsafe { vy_node_append(self.raw.as_ptr(), parent.to_ffi(), child.to_ffi()) })
    }

    pub fn parent(&self, node: NodeId) -> Result<Option<NodeId>, VeyraError> {
        let mut raw = VyNodeId {
            index: u32::MAX,
            generation: 0,
        };
        check(unsafe { vy_node_parent(self.raw.as_ptr(), node.to_ffi(), &mut raw) })?;
        Ok(NodeId::from_ffi(raw))
    }

    pub fn len(&self) -> u64 {
        unsafe { vy_node_count(self.raw.as_ptr()) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl Drop for NodeTree {
    fn drop(&mut self) {
        unsafe { vy_node_tree_destroy(self.raw.as_ptr()) };
    }
}
