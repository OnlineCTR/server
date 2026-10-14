# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

{ lib
, stdenv
, cmake
, pkg-config
, curl
, withDebug ? true
}:

let
  isWindows = stdenv.hostPlatform.uname.system == "Windows";

  mainProgram = if isWindows then "ctr_srv.exe" else "ctr_srv";
in
stdenv.mkDerivation (_: {
  pname = "CTR-SRV";
  version = "0.0.1";

  src = ./.;

  nativeBuildInputs = [ cmake pkg-config ];
  # ENet is bundled (enet/); libcurl for the heartbeat (without it the build
  # leaves the heartbeat out)
  buildInputs = lib.optionals (!isWindows) [ curl ];

  # Disables incompatible hardening
  hardeningDisable = [ "format" ];

  # Config
  cmakeFlags = lib.optionals withDebug [ "-DCMAKE_BUILD_TYPE=Debug" ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp ${mainProgram} $out/bin/

    runHook postInstall
  '';

  # Keep debug symbols
  dontStrip = withDebug;

  # Shows the proper compile date in the logs
  env.SOURCE_DATE_EPOCH = (builtins.currentTime or 1);

  meta = {
    description = "CTR Online Companion";
    homepage = "https://github.com/OnlineCTR/server";
    license = lib.licenses.agpl3Only;
    maintainers = with lib.maintainers; [ pedrohlc ];
    mainProgram = "ctr_srv";
    platforms = lib.platforms.all;
  };
})
