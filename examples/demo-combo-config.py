"""
Demo: Combo Rules Configuration
This example shows how to configure combo rules using the config-default.yaml
or a custom configuration file.
"""

from clause.config.options import Options

# Load default options
opts = Options()

# Display combo-related options
print("=" * 60)
print("Default Combo Configuration:")
print("=" * 60)

loader_opts = opts.flat("loader")
combo_keys = [k for k in loader_opts.keys() if 'combo' in k]

for key in sorted(combo_keys):
    full_key = f"loader.{key}"
    print(f"{key:25} = {loader_opts[key]}")

print("\n" + "=" * 60)
print("Modifying Combo Configuration:")
print("=" * 60)

# Modify combo options programmatically
opts.set("loader.load_combo", True)
opts.set("loader.combo_debug", True)
opts.set("loader.combo_min_pred", 10)
opts.set("loader.combo_min_support", 5)
opts.set("loader.combo_min_conf", 0.001)

print("Setting load_combo = True")
print("Setting combo_debug = True")
print("Setting combo_min_pred = 10")
print("Setting combo_min_support = 5")
print("Setting combo_min_conf = 0.001")

print("\n" + "=" * 60)
print("Updated Combo Configuration:")
print("=" * 60)

loader_opts = opts.flat("loader")
for key in sorted(combo_keys):
    print(f"{key:25} = {loader_opts[key]}")

print("\n" + "=" * 60)
print("How to Use Custom Config File:")
print("=" * 60)
print("""
# Create config-my.yaml with:
loader:
  load_combo: True
  combo_debug: True
  combo_min_pred: 10
  combo_min_support: 5
  combo_min_conf: 0.001

# Load with custom config:
opts = Options(path='config-my.yaml')
""")

print("=" * 60)
