package veyra_test

import (
	"testing"

	veyra "github.com/SimaoMoreira5228/veyra/bindings/go"
)

func TestAbiVersionIsExposed(t *testing.T) {
	if veyra.AbiVersion() != 1 {
		t.Fatalf("abi version = %d, want 1", veyra.AbiVersion())
	}
}

func TestHeadlessThemePropertyAndTree(t *testing.T) {
	theme, err := veyra.NewTheme(veyra.Dark)
	if err != nil {
		t.Fatalf("new theme: %v", err)
	}
	defer theme.Close()

	if err := theme.SetColor(veyra.ColorActionPrimary, veyra.RGBA{10, 20, 30, 255}, veyra.LayerOverride); err != nil {
		t.Fatalf("set colour: %v", err)
	}
	color, err := theme.Color(veyra.ColorActionPrimary)
	if err != nil {
		t.Fatalf("read colour: %v", err)
	}
	if color != (veyra.RGBA{10, 20, 30, 255}) {
		t.Fatalf("colour = %v", color)
	}

	if err := theme.SetSpace(veyra.SpaceLarge, 22, veyra.LayerOverride); err != nil {
		t.Fatalf("set spacing: %v", err)
	}
	space, err := theme.Space(veyra.SpaceLarge)
	if err != nil || space != 22 {
		t.Fatalf("spacing = %v, err = %v", space, err)
	}
	if err := theme.SetRadius(veyra.RadiusSmall, 3, veyra.LayerOverride); err != nil {
		t.Fatalf("set radius: %v", err)
	}
	radius, err := theme.Radius(veyra.RadiusSmall)
	if err != nil || radius != 3 {
		t.Fatalf("radius = %v, err = %v", radius, err)
	}
	if err := theme.SetMotion(veyra.MotionFast, 80, veyra.LayerOverride); err != nil {
		t.Fatalf("set motion: %v", err)
	}
	motion, err := theme.Motion(veyra.MotionFast)
	if err != nil || motion != 80 {
		t.Fatalf("motion = %v, err = %v", motion, err)
	}

	if err := theme.Apply("schema: 1\ncolor_surface = #102030\n"); err != nil {
		t.Fatalf("apply theme: %v", err)
	}
	surface, err := theme.Color(veyra.ColorSurface)
	if err != nil {
		t.Fatalf("read surface: %v", err)
	}
	if surface != (veyra.RGBA{16, 32, 48, 255}) {
		t.Fatalf("surface = %v", surface)
	}

	properties, err := veyra.NewPropertySet()
	if err != nil {
		t.Fatalf("new property set: %v", err)
	}
	defer properties.Close()

	if err := properties.ApplyTheme(theme); err != nil {
		t.Fatalf("apply theme to properties: %v", err)
	}
	background, err := properties.Resolve(veyra.PropertyBackground)
	if err != nil {
		t.Fatalf("resolve background: %v", err)
	}
	if !background.Set || background.Layer != veyra.StyleTheme {
		t.Fatalf("background provenance = %+v", background)
	}
	if background.Value.Kind != veyra.ValueColor || background.Value.Color != (veyra.RGBA{16, 32, 48, 255}) {
		t.Fatalf("background value = %+v", background.Value)
	}
	if background.Token == nil || background.Token.Token != veyra.ColorSurface {
		t.Fatalf("background token = %+v", background.Token)
	}

	if err := properties.SetColor(veyra.PropertyForeground, veyra.StyleLocal, veyra.RGBA{200, 10, 20, 255}); err != nil {
		t.Fatalf("local override: %v", err)
	}
	foreground, err := properties.Resolve(veyra.PropertyForeground)
	if err != nil {
		t.Fatalf("resolve foreground: %v", err)
	}
	if foreground.Layer != veyra.StyleLocal || foreground.Token != nil {
		t.Fatalf("foreground provenance = %+v", foreground)
	}
	if err := properties.SetLength(veyra.PropertyForeground, veyra.StyleLocal, 4); err == nil {
		t.Fatal("a length value on a colour property was accepted")
	}

	tree, err := veyra.NewNodeTree()
	if err != nil {
		t.Fatalf("new tree: %v", err)
	}
	defer tree.Close()

	if err := tree.Reserve(8); err != nil {
		t.Fatalf("reserve: %v", err)
	}
	root, err := tree.NewNode()
	if err != nil {
		t.Fatalf("root node: %v", err)
	}
	child, err := tree.NewNode()
	if err != nil {
		t.Fatalf("child node: %v", err)
	}
	if err := tree.Append(root, child); err != nil {
		t.Fatalf("append: %v", err)
	}
	if tree.Len() != 2 {
		t.Fatalf("node count = %d", tree.Len())
	}

	parent, ok, err := tree.Parent(child)
	if err != nil || !ok || parent != root {
		t.Fatalf("child parent = %v, %v, %v", parent, ok, err)
	}
	if _, ok, err = tree.Parent(root); err != nil || ok {
		t.Fatalf("root parent = %v, %v", ok, err)
	}
	if err := tree.Append(root, root); err == nil {
		t.Fatal("a node was appended to itself")
	}
}

func TestThemeFileErrorsCarryLocation(t *testing.T) {
	theme, err := veyra.NewTheme(veyra.Light)
	if err != nil {
		t.Fatalf("new theme: %v", err)
	}
	defer theme.Close()

	err = theme.Apply("schema: 1\ncolor_missing = #ffffff\n")
	fileError, ok := err.(*veyra.FileError)
	if !ok {
		t.Fatalf("error = %T, want *veyra.FileError", err)
	}
	if fileError.Line != 2 || fileError.Column != 1 {
		t.Fatalf("location = %d:%d", fileError.Line, fileError.Column)
	}
}

func TestClosedHandlesRejectCalls(t *testing.T) {
	theme, err := veyra.NewTheme(veyra.Light)
	if err != nil {
		t.Fatalf("new theme: %v", err)
	}
	theme.Close()
	theme.Close()
	if _, err := theme.Color(veyra.ColorSurface); err == nil {
		t.Fatal("a closed theme accepted a read")
	}

	tree, err := veyra.NewNodeTree()
	if err != nil {
		t.Fatalf("new tree: %v", err)
	}
	tree.Close()
	if _, err := tree.NewNode(); err == nil {
		t.Fatal("a closed tree accepted a node")
	}
	if tree.Len() != 0 {
		t.Fatal("a closed tree reported nodes")
	}
}
