#!/usr/bin/env bash
set -eu

CUR_DIR="${PWD}"
cd "$(dirname "${BASH_SOURCE:-$0}")"

# Load environment variables from .env file if it exists
if [ -f ".env" ]; then
  set -a
  source .env
  set +a
fi

INSTALL_TOOLS=1
REBUILD=0
SKIP_TESTS=0
CREATE_DOCS=0
CREATE_ZIP=0
CREATE_INSTALLER=0
SIGN_INI=0
CMAKE_BUILD_TYPE=Release
ARCHS="x86_64"
USE_ADDRESS_SANITIZER=OFF
while [[ $# -gt 0 ]]; do
  case $1 in
    -d|--debug)
      CMAKE_BUILD_TYPE=Debug
      shift
      ;;
    -a|--arch)
      ARCHS="$2"
      shift 2
      ;;
    -r|--rebuild)
      REBUILD=1
      shift
      ;;
    -s|--skip-tests)
      SKIP_TESTS=1
      shift
      ;;
    -z|--zip)
      CREATE_ZIP=1
      CREATE_DOCS=1
      shift
      ;;
    -i|--installer)
      CREATE_INSTALLER=1
      shift
      ;;
    sign-ini)
      SIGN_INI=1
      shift
      ;;
    --asan)
      USE_ADDRESS_SANITIZER=ON
      shift
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      shift
      ;;
  esac
done

if [ "${INSTALL_TOOLS}" -eq 1 ]; then
  mkdir -p "build/tools"
  . "${PWD}/src/c/3rd/ovbase/setup-llvm-mingw.sh" --dir "${PWD}/build/tools"

  case "$(uname -s)" in
    MINGW64_NT* | MINGW32_NT*)
      SEVENZIP_URL="https://www.7-zip.org/a/7z2501-extra.7z"
      SEVENZIP_DIR="${PWD}/build/tools/7z2501-windows"
      SEVENZIP_ARCHIVE="${PWD}/build/tools/$(basename "${SEVENZIP_URL}")"
      if [ ! -d "${SEVENZIP_DIR}" ]; then
        if [ ! -f "${SEVENZIP_ARCHIVE}" ]; then
          echo "Downloading: ${SEVENZIP_URL}"
          curl -o "${SEVENZIP_ARCHIVE}" -sOL "$SEVENZIP_URL"
        fi
        mkdir -p "${SEVENZIP_DIR}"
        (cd "${SEVENZIP_DIR}" && cmake -E tar xf "${SEVENZIP_ARCHIVE}")
      fi
      export PATH="${SEVENZIP_DIR}:$PATH"
      ;;
    *)
      ;;
  esac
fi

# Skip normal build process if only sign-ini or installer is requested
if [ "${SIGN_INI}" -eq 0 ] && [ "${CREATE_INSTALLER}" -eq 0 ] || [ "${CREATE_ZIP}" -eq 1 ]; then
  for arch in $ARCHS; do
    builddir="${PWD}/build/${CMAKE_BUILD_TYPE}/${arch}"
    if [ "${REBUILD}" -eq 1 ] || [ ! -e "${builddir}/CMakeCache.txt" ]; then
      rm -rf "${builddir}"
      cmake -S . -B "${builddir}" --preset debug \
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
        -DCMAKE_TOOLCHAIN_FILE="src/c/3rd/ovbase/cmake/llvm-mingw.cmake" \
        -DCMAKE_C_COMPILER="${arch}-w64-mingw32-clang" \
        -DUSE_ADDRESS_SANITIZER="${USE_ADDRESS_SANITIZER}"
    fi
    cmake --build "${builddir}"
    if [ "${SKIP_TESTS}" -eq 0 ]; then
      ctest --test-dir "${builddir}" --output-on-failure --output-junit testlog.xml
    fi
  done
fi

if [ "${CREATE_ZIP}" -eq 1 ] || [ "${CREATE_INSTALLER}" -eq 1 ]; then
  # Generate version.env
  builddir="${PWD}/build/${CMAKE_BUILD_TYPE}/x86_64"
  version_env="${builddir}/src/c/version.env"
  cmake \
    -Dlocal_dir="${PWD}" \
    -Dinput_file="${PWD}/src/c/version.h.in" \
    -Doutput_file="${builddir}/src/c/version.h" \
    -P "${PWD}/src/cmake/version.cmake"
  if [ ! -f "${version_env}" ]; then
    echo "Error: Failed to generate version.env"
    exit 1
  fi
  source "${version_env}"
  echo "Version: ${GCMZ_VERSION}"
fi

if [ "${CREATE_ZIP}" -eq 1 ]; then
  distdir="${PWD}/build/${CMAKE_BUILD_TYPE}/dist"
  rm -rf "${distdir}"
  mkdir -p "${distdir}"
  builddir="${PWD}/build/${CMAKE_BUILD_TYPE}/x86_64"
  zipname="gcmzdrops_${GCMZ_VERSION}.zip"
  (cd "${builddir}/bin" && cmake -E tar cf "${distdir}/${zipname}" --format=zip .)
fi

if [ "${CREATE_INSTALLER}" -eq 1 ]; then
  distdir="${PWD}/build/${CMAKE_BUILD_TYPE}/dist"
  mkdir -p "${distdir}"
  builddir="${PWD}/build/${CMAKE_BUILD_TYPE}/x86_64"
  installer_iss="${builddir}/installer.iss"

  # Generate installer script using CMake
  cmake \
    -Dlocal_dir="${PWD}" \
    -Dinput_file="${PWD}/installer.iss.in" \
    -Doutput_file="${installer_iss}" \
    -Dbuild_output_dir="${builddir}/bin" \
    -Dwork_dir="${builddir}" \
    -Doutput_dir="${distdir}" \
    -Dversion="${GCMZ_VERSION}" \
    -P "${PWD}/src/cmake/installer.cmake"

  # Find Inno Setup compiler
  ISCC_PATH=""
  for path in \
    "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "/c/Program Files/Inno Setup 6/ISCC.exe" \
    "$(command -v iscc 2>/dev/null || true)"; do
    if [ -f "$path" ]; then
      ISCC_PATH="$path"
      break
    fi
  done

  if [ -z "${ISCC_PATH}" ]; then
    echo "Warning: Inno Setup compiler (ISCC.exe) not found."
    echo "Installer script generated at: ${installer_iss}"
    echo "You can compile it manually with Inno Setup."
  else
    echo "Building installer with: ${ISCC_PATH}"
    "${ISCC_PATH}" "${installer_iss}"
  fi
fi

if [ "${SIGN_INI}" -eq 1 ]; then
  if [ -z "${GCMZ_SECRET_KEY}" ]; then
    echo "Error: GCMZ_SECRET_KEY environment variable is not set."
    echo "Please set it in .env file or export it directly."
    exit 1
  fi

  ini_signer_exe="${PWD}/build/${CMAKE_BUILD_TYPE}/x86_64/src/c/ini_signer.exe"
  ini_file="${PWD}/src/c/aviutl2_addr.ini"

  if [ ! -f "${ini_signer_exe}" ]; then
    echo "Error: ini_signer.exe not found at ${ini_signer_exe}"
    echo "Please run build first to create the executable."
    exit 1
  fi

  if [ ! -f "${ini_file}" ]; then
    echo "Error: aviutl2_addr.ini not found at ${ini_file}"
    exit 1
  fi

  echo "Signing INI file: ${ini_file}"
  echo "Using ini_signer: ${ini_signer_exe}"
  "${ini_signer_exe}" sign "${ini_file}"
fi

echo "Build script completed."
