#! /usr/bin/env nix-build
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

{ pkgs ? import <nixpkgs> {}
, pkgsCross ? pkgs.pkgsi686Linux
}:

pkgsCross.callPackage ./default.nix {}
