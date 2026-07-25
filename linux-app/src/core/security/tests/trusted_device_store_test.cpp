#include "Check.hpp"
#include <filesystem>
#include <iostream>

#include "TrustedDeviceStore.hpp"

using namespace wiremic::security;

int main() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "wiremic_trusted_devices_test.json";
  std::filesystem::remove(path);

  {
    TrustedDeviceStore store(path);
    WIREMIC_CHECK(!store.IsTrusted("device-1", "sha256:aaaa"));
    store.Trust("device-1", "Pixel 8", "sha256:aaaa");
    WIREMIC_CHECK(store.IsTrusted("device-1", "sha256:aaaa"));
    WIREMIC_CHECK(!store.IsTrusted("device-1", "sha256:bbbb"));
    WIREMIC_CHECK(!store.IsTrusted("device-2", "sha256:aaaa"));
  }

  {
    TrustedDeviceStore reloaded(path);
    WIREMIC_CHECK(reloaded.IsTrusted("device-1", "sha256:aaaa"));
    auto found = reloaded.Find("device-1");
    WIREMIC_CHECK(found.has_value());
    WIREMIC_CHECK(found->name == "Pixel 8");

    reloaded.Trust("device-2", "Galaxy S24", "sha256:cccc");
    WIREMIC_CHECK(reloaded.All().size() == 2);

    reloaded.Revoke("device-1");
    WIREMIC_CHECK(!reloaded.IsTrusted("device-1", "sha256:aaaa"));
    WIREMIC_CHECK(reloaded.All().size() == 1);
  }

  {
    TrustedDeviceStore finalCheck(path);
    WIREMIC_CHECK(finalCheck.All().size() == 1);
    WIREMIC_CHECK(finalCheck.IsTrusted("device-2", "sha256:cccc"));
  }

  std::filesystem::remove(path);
  std::cout << "TRUSTED_DEVICE_STORE_TESTS_PASSED\n";
  return 0;
}
