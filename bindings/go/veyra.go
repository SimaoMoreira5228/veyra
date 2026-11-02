package veyra

/*
// The static Veyra archives are linked from CGO_LDFLAGS=-L<build dir>.
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -lveyra_c -lveyra_style -lveyra_foundation
#cgo linux LDFLAGS: -lstdc++ -lm
#cgo darwin LDFLAGS: -lc++
#cgo windows LDFLAGS: -lstdc++
#include "veyra/c/veyra.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// AbiVersion reports the C ABI version compiled into the linked library.
func AbiVersion() uint32 {
	return uint32(C.vy_abi_version())
}

func statusName(status int32) string {
	name := C.vy_status_name(C.int32_t(status))
	if name == nil {
		return "unknown"
	}
	return C.GoString(name)
}

// Error is a status code returned by a C ABI call.
type Error struct {
	Status int32
}

func (e *Error) Error() string {
	return statusName(e.Status)
}

// FileError carries the location of a rejected theme file.
type FileError struct {
	Status int32
	Line   uint32
	Column uint32
}

func (e *FileError) Error() string {
	return fmt.Sprintf("%s at %d:%d", statusName(e.Status), e.Line, e.Column)
}

func check(status C.int32_t) error {
	if status == C.VY_OK {
		return nil
	}
	return &Error{Status: int32(status)}
}

// Variant selects a built-in theme.
type Variant int32

const (
	Light        Variant = C.VY_THEME_LIGHT
	Dark         Variant = C.VY_THEME_DARK
	HighContrast Variant = C.VY_THEME_HIGH_CONTRAST
)

// Theme layers, ordered from weakest to strongest.
const (
	LayerDefaults uint8 = C.VY_LAYER_DEFAULTS
	LayerPreset   uint8 = C.VY_LAYER_PRESET
	LayerFile     uint8 = C.VY_LAYER_FILE
	LayerOverride uint8 = C.VY_LAYER_OVERRIDE
)

// Style layers, ordered from weakest to strongest.
const (
	StylePlatformDefault uint8 = C.VY_STYLE_PLATFORM_DEFAULT
	StyleTheme           uint8 = C.VY_STYLE_THEME
	StyleRecipe          uint8 = C.VY_STYLE_RECIPE
	StyleVariant         uint8 = C.VY_STYLE_VARIANT
	StyleState           uint8 = C.VY_STYLE_STATE
	StyleLocal           uint8 = C.VY_STYLE_LOCAL
	StyleController      uint8 = C.VY_STYLE_CONTROLLER
	StyleAccessibility   uint8 = C.VY_STYLE_ACCESSIBILITY
)

// Color tokens.
const (
	ColorSurface         uint8 = C.VY_COLOR_SURFACE
	ColorTextPrimary     uint8 = C.VY_COLOR_TEXT_PRIMARY
	ColorActionPrimary   uint8 = C.VY_COLOR_ACTION_PRIMARY
	ColorActionOnPrimary uint8 = C.VY_COLOR_ACTION_ON_PRIMARY
	ColorActionHovered   uint8 = C.VY_COLOR_ACTION_HOVERED
	ColorActionPressed   uint8 = C.VY_COLOR_ACTION_PRESSED
	ColorActionDisabled  uint8 = C.VY_COLOR_ACTION_DISABLED
	ColorTextDisabled    uint8 = C.VY_COLOR_TEXT_DISABLED
	ColorToggleChecked   uint8 = C.VY_COLOR_TOGGLE_CHECKED
	ColorToggleOnChecked uint8 = C.VY_COLOR_TOGGLE_ON_CHECKED
	ColorError           uint8 = C.VY_COLOR_ERROR
)

// Space tokens.
const (
	SpaceNone       uint8 = C.VY_SPACE_NONE
	SpaceExtraSmall uint8 = C.VY_SPACE_EXTRA_SMALL
	SpaceSmall      uint8 = C.VY_SPACE_SMALL
	SpaceMedium     uint8 = C.VY_SPACE_MEDIUM
	SpaceLarge      uint8 = C.VY_SPACE_LARGE
	SpaceExtraLarge uint8 = C.VY_SPACE_EXTRA_LARGE
)

// Radius tokens.
const (
	RadiusNone   uint8 = C.VY_RADIUS_NONE
	RadiusSmall  uint8 = C.VY_RADIUS_SMALL
	RadiusMedium uint8 = C.VY_RADIUS_MEDIUM
	RadiusLarge  uint8 = C.VY_RADIUS_LARGE
)

// Motion tokens.
const (
	MotionInstant uint8 = C.VY_MOTION_INSTANT
	MotionFast    uint8 = C.VY_MOTION_FAST
	MotionNormal  uint8 = C.VY_MOTION_NORMAL
	MotionSlow    uint8 = C.VY_MOTION_SLOW
)

// Style properties.
const (
	PropertyForeground         uint8 = C.VY_PROPERTY_FOREGROUND
	PropertyBackground         uint8 = C.VY_PROPERTY_BACKGROUND
	PropertyBorderColor        uint8 = C.VY_PROPERTY_BORDER_COLOR
	PropertyCornerRadius       uint8 = C.VY_PROPERTY_CORNER_RADIUS
	PropertyBorderWidth        uint8 = C.VY_PROPERTY_BORDER_WIDTH
	PropertyPadding            uint8 = C.VY_PROPERTY_PADDING
	PropertyFontSize           uint8 = C.VY_PROPERTY_FONT_SIZE
	PropertyOpacity            uint8 = C.VY_PROPERTY_OPACITY
	PropertyTransitionDuration uint8 = C.VY_PROPERTY_TRANSITION_DURATION
)

// ValueKind identifies the active member of a PropertyValue.
type ValueKind uint8

const (
	ValueColor    ValueKind = C.VY_VALUE_COLOR
	ValueLength   ValueKind = C.VY_VALUE_LENGTH
	ValueDuration ValueKind = C.VY_VALUE_DURATION
	ValueNumber   ValueKind = C.VY_VALUE_NUMBER
)

// TokenKind identifies the token namespace of a TokenRef.
type TokenKind uint8

const (
	TokenColor  TokenKind = C.VY_TOKEN_COLOR
	TokenSpace  TokenKind = C.VY_TOKEN_SPACE
	TokenRadius TokenKind = C.VY_TOKEN_RADIUS
	TokenMotion TokenKind = C.VY_TOKEN_MOTION
)

// RGBA is a straight-alpha color.
type RGBA [4]uint8

// Theme wraps the opaque vy_theme handle. Close releases it; every method
// after Close returns an Error.
type Theme struct {
	raw *C.vy_theme
}

// NewTheme creates a built-in theme variant.
func NewTheme(variant Variant) (*Theme, error) {
	var raw *C.vy_theme
	if err := check(C.vy_theme_create(C.int32_t(variant), &raw)); err != nil {
		return nil, err
	}
	if raw == nil {
		return nil, &Error{Status: int32(C.VY_OUT_OF_MEMORY)}
	}
	return &Theme{raw: raw}, nil
}

// Close releases the theme. It is safe to call more than once.
func (t *Theme) Close() {
	if t == nil || t.raw == nil {
		return
	}
	C.vy_theme_destroy(t.raw)
	t.raw = nil
}

// SetColor writes a color token at the given theme layer.
func (t *Theme) SetColor(token uint8, color RGBA, layer uint8) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_theme_set_color(
		t.raw,
		C.uint8_t(token),
		C.uint8_t(color[0]),
		C.uint8_t(color[1]),
		C.uint8_t(color[2]),
		C.uint8_t(color[3]),
		C.uint8_t(layer),
	))
}

// Color reads a resolved color token.
func (t *Theme) Color(token uint8) (RGBA, error) {
	if t.raw == nil {
		return RGBA{}, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var rgba [4]C.uint8_t
	if err := check(C.vy_theme_color(t.raw, C.uint8_t(token), &rgba[0])); err != nil {
		return RGBA{}, err
	}
	return RGBA{uint8(rgba[0]), uint8(rgba[1]), uint8(rgba[2]), uint8(rgba[3])}, nil
}

// SetSpace writes a spacing token at the given theme layer.
func (t *Theme) SetSpace(token uint8, value float32, layer uint8) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_theme_set_space(t.raw, C.uint8_t(token), C.float(value), C.uint8_t(layer)))
}

// Space reads a spacing token.
func (t *Theme) Space(token uint8) (float32, error) {
	if t.raw == nil {
		return 0, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var value C.float
	if err := check(C.vy_theme_space(t.raw, C.uint8_t(token), &value)); err != nil {
		return 0, err
	}
	return float32(value), nil
}

// SetRadius writes a radius token at the given theme layer.
func (t *Theme) SetRadius(token uint8, value float32, layer uint8) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_theme_set_radius(t.raw, C.uint8_t(token), C.float(value), C.uint8_t(layer)))
}

// Radius reads a radius token.
func (t *Theme) Radius(token uint8) (float32, error) {
	if t.raw == nil {
		return 0, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var value C.float
	if err := check(C.vy_theme_radius(t.raw, C.uint8_t(token), &value)); err != nil {
		return 0, err
	}
	return float32(value), nil
}

// SetMotion writes a motion token at the given theme layer.
func (t *Theme) SetMotion(token uint8, value uint32, layer uint8) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_theme_set_motion(t.raw, C.uint8_t(token), C.uint32_t(value), C.uint8_t(layer)))
}

// Motion reads a motion token in milliseconds.
func (t *Theme) Motion(token uint8) (uint32, error) {
	if t.raw == nil {
		return 0, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var value C.uint32_t
	if err := check(C.vy_theme_motion(t.raw, C.uint8_t(token), &value)); err != nil {
		return 0, err
	}
	return uint32(value), nil
}

// Apply overlays a theme file. A rejected file returns *FileError.
func (t *Theme) Apply(text string) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var error C.vy_theme_error
	var data *C.char
	if len(text) != 0 {
		data = (*C.char)(unsafe.Pointer(unsafe.StringData(text)))
	}
	status := C.vy_theme_apply(t.raw, C.vy_utf8{data: data, size: C.uint64_t(len(text))}, &error)
	if status == C.VY_OK {
		return nil
	}
	return &FileError{
		Status: int32(status),
		Line:   uint32(error.line),
		Column: uint32(error.column),
	}
}

// PropertyValue holds one typed style value.
type PropertyValue struct {
	Kind     ValueKind
	Color    RGBA
	Length   float32
	Duration uint32
	Number   float32
}

// TokenRef names the token that supplied a resolved value.
type TokenRef struct {
	Kind  TokenKind
	Token uint8
	Line  uint32
}

// ResolvedProperty is one style property after layer resolution.
type ResolvedProperty struct {
	Property uint8
	Set      bool
	Value    PropertyValue
	Layer    uint8
	Token    *TokenRef
}

// PropertySet wraps the opaque vy_property_set handle.
type PropertySet struct {
	raw *C.vy_property_set
}

// NewPropertySet creates an empty layered property set.
func NewPropertySet() (*PropertySet, error) {
	var raw *C.vy_property_set
	if err := check(C.vy_property_set_create(&raw)); err != nil {
		return nil, err
	}
	if raw == nil {
		return nil, &Error{Status: int32(C.VY_OUT_OF_MEMORY)}
	}
	return &PropertySet{raw: raw}, nil
}

// Close releases the property set. It is safe to call more than once.
func (s *PropertySet) Close() {
	if s == nil || s.raw == nil {
		return
	}
	C.vy_property_set_destroy(s.raw)
	s.raw = nil
}

// ApplyTheme fills the theme layer from a theme.
func (s *PropertySet) ApplyTheme(theme *Theme) error {
	if s.raw == nil || theme == nil || theme.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_property_set_apply_theme(s.raw, theme.raw))
}

// SetColor writes a color property at a style layer.
func (s *PropertySet) SetColor(property uint8, layer uint8, color RGBA) error {
	if s.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_property_set_set_color(
		s.raw,
		C.uint8_t(property),
		C.uint8_t(layer),
		C.uint8_t(color[0]),
		C.uint8_t(color[1]),
		C.uint8_t(color[2]),
		C.uint8_t(color[3]),
	))
}

// SetLength writes a length property in logical pixels at a style layer.
func (s *PropertySet) SetLength(property uint8, layer uint8, value float32) error {
	if s.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_property_set_set_length(
		s.raw,
		C.uint8_t(property),
		C.uint8_t(layer),
		C.float(value),
	))
}

// SetDuration writes a duration property in milliseconds at a style layer.
func (s *PropertySet) SetDuration(property uint8, layer uint8, value uint32) error {
	if s.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_property_set_set_duration(
		s.raw,
		C.uint8_t(property),
		C.uint8_t(layer),
		C.uint32_t(value),
	))
}

// SetNumber writes a unit-interval number property at a style layer.
func (s *PropertySet) SetNumber(property uint8, layer uint8, value float32) error {
	if s.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_property_set_set_number(
		s.raw,
		C.uint8_t(property),
		C.uint8_t(layer),
		C.float(value),
	))
}

// Resolve returns the value and provenance of a property.
func (s *PropertySet) Resolve(property uint8) (ResolvedProperty, error) {
	if s.raw == nil {
		return ResolvedProperty{}, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var resolved C.vy_resolved_property
	if err := check(C.vy_property_set_resolve(s.raw, C.uint8_t(property), &resolved)); err != nil {
		return ResolvedProperty{}, err
	}

	value := PropertyValue{}
	switch ValueKind(resolved.value_kind) {
	case ValueColor:
		value = PropertyValue{
			Kind:  ValueColor,
			Color: RGBA{uint8(resolved.red), uint8(resolved.green), uint8(resolved.blue), uint8(resolved.alpha)},
		}
	case ValueLength:
		value = PropertyValue{Kind: ValueLength, Length: float32(resolved.length)}
	case ValueDuration:
		value = PropertyValue{Kind: ValueDuration, Duration: uint32(resolved.duration_ms)}
	case ValueNumber:
		value = PropertyValue{Kind: ValueNumber, Number: float32(resolved.number)}
	default:
		return ResolvedProperty{}, &Error{Status: int32(C.VY_INVALID_VALUE)}
	}

	result := ResolvedProperty{
		Property: uint8(resolved.property),
		Set:      resolved.set != 0,
		Value:    value,
		Layer:    uint8(resolved.layer),
	}
	if resolved.from_token != 0 {
		result.Token = &TokenRef{
			Kind:  TokenKind(resolved.token_kind),
			Token: uint8(resolved.token),
			Line:  uint32(resolved.line),
		}
	}
	return result, nil
}

// NodeId is a generational node identity.
type NodeId struct {
	Index      uint32
	Generation uint32
}

// NodeTree wraps the opaque vy_node_tree handle.
type NodeTree struct {
	raw *C.vy_node_tree
}

// NewNodeTree creates an empty node tree.
func NewNodeTree() (*NodeTree, error) {
	var raw *C.vy_node_tree
	if err := check(C.vy_node_tree_create(&raw)); err != nil {
		return nil, err
	}
	if raw == nil {
		return nil, &Error{Status: int32(C.VY_OUT_OF_MEMORY)}
	}
	return &NodeTree{raw: raw}, nil
}

// Close releases the node tree. It is safe to call more than once.
func (t *NodeTree) Close() {
	if t == nil || t.raw == nil {
		return
	}
	C.vy_node_tree_destroy(t.raw)
	t.raw = nil
}

// Reserve preallocates storage for count nodes.
func (t *NodeTree) Reserve(count uint64) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_node_tree_reserve(t.raw, C.uint64_t(count)))
}

// NewNode creates a detached node.
func (t *NodeTree) NewNode() (NodeId, error) {
	if t.raw == nil {
		return NodeId{}, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var node C.vy_node_id
	if err := check(C.vy_node_create(t.raw, &node)); err != nil {
		return NodeId{}, err
	}
	if node.index == C.UINT32_MAX {
		return NodeId{}, &Error{Status: int32(C.VY_INVALID_OPERATION)}
	}
	return NodeId{Index: uint32(node.index), Generation: uint32(node.generation)}, nil
}

// Append attaches child under parent.
func (t *NodeTree) Append(parent NodeId, child NodeId) error {
	if t.raw == nil {
		return &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	return check(C.vy_node_append(t.raw, toC(parent), toC(child)))
}

// Parent returns the parent of a node, or false for a root.
func (t *NodeTree) Parent(node NodeId) (NodeId, bool, error) {
	if t.raw == nil {
		return NodeId{}, false, &Error{Status: int32(C.VY_INVALID_HANDLE)}
	}
	var parent C.vy_node_id
	if err := check(C.vy_node_parent(t.raw, toC(node), &parent)); err != nil {
		return NodeId{}, false, err
	}
	if parent.index == C.UINT32_MAX {
		return NodeId{}, false, nil
	}
	return NodeId{Index: uint32(parent.index), Generation: uint32(parent.generation)}, true, nil
}

// Len reports the live node count.
func (t *NodeTree) Len() uint64 {
	if t.raw == nil {
		return 0
	}
	return uint64(C.vy_node_count(t.raw))
}

func toC(node NodeId) C.vy_node_id {
	return C.vy_node_id{index: C.uint32_t(node.Index), generation: C.uint32_t(node.Generation)}
}
