{
	pkgs ? import <nixpkgs> { }
}:

pkgs.mkShell {
	buildInputs = with pkgs; [
		clang-tools
		bear
		lldb
		pkg-config

		pipewire

		xdo
		xdotool
		xorg.libXtst
		xorg.libX11
		xorg.libxcb
		xorg.xinput
		xorg.libXi
		libevdev
		libxkbcommon
	];
}

