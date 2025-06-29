#!/bin/bash
set -e

# === Configuration ===
DOTNET_DIR="/opt/dotnet"
DOTNET_VERSIONS="6.0.420 7.0.400 8.0.100"

LATEST_RUNTIME="7.0.10"  # The 'latest' symlinks for ReCoDex
LATEST_SDK="7.0.400"

ARCH="x64"

# === Create base install dir ===
mkdir -p "$DOTNET_DIR"

#=== Install SDKs and Runtimes ===
for VERSION in $DOTNET_VERSIONS; do
    echo "Installing .NET SDK $VERSION..."

    echo "Downloading .NET SDK version $VERSION..."
    DOTNET_SDK_URL="https://dotnetcli.azureedge.net/dotnet/Sdk/${VERSION}/dotnet-sdk-${VERSION}-linux-x64.tar.gz"
    curl -sSL "$DOTNET_SDK_URL" -o /tmp/dotnet-sdk.tar.gz

    echo "Extracting..."
    tar -xzf /tmp/dotnet-sdk.tar.gz -C "$DOTNET_DIR"
    rm /tmp/dotnet-sdk.tar.gz

done

# === Set Latest Version for ReCoDex ===
SDK_DIR="$DOTNET_DIR/sdk/$LATEST_SDK"
RUNTIME_DIR="$DOTNET_DIR/shared/Microsoft.NETCore.App/$LATEST_RUNTIME"

# Ensure the target dirs exist
if [[ -d "$SDK_DIR" && -d "$RUNTIME_DIR" ]]; then
    ln -sfn "$SDK_DIR" "$DOTNET_DIR/sdk/latest"
    ln -sfn "$RUNTIME_DIR" "$DOTNET_DIR/shared/Microsoft.NETCore.App/latest"
    echo "Created 'latest' symlinks for SDK and runtime"
else
    echo "Could not create 'latest' links — check if $LATEST was installed correctly"
fi

echo "Finished installing: $DOTNET_VERSIONS"
