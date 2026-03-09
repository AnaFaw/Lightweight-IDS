import glob, os
import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import classification_report, confusion_matrix

WINDOW_S = 10.0

def infer_label(filename: str) -> str:
    name = os.path.basename(filename).lower()
    if name.startswith("flood_"): return "attack"
    if name.startswith("scan_"): return "attack"
    if name.startswith("bruteforce_parallel_"): return "attack"
    if name.startswith("normal_"): return "normal"
    return "skip"

def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    if "timestamp" not in df.columns:
        raise ValueError(f"{path} saknar timestamp. Använd parallel-scripts som loggar timestamp.")
    df["status_str"] = df["status"].astype(str)
    return df

def to_window_features(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()
    t0 = df["timestamp"].min()
    df["w"] = ((df["timestamp"] - t0) // WINDOW_S).astype(int)

    rows = []
    for w, g in df.groupby("w"):
        req = len(g)
        fail = (g["status_str"] == "401").sum()
        nf   = (g["status_str"] == "404").sum()
        nete = (g["status_str"] == "NET_ERR").sum()

        rows.append({
            "reqRate": req / WINDOW_S,
            "failRate": fail / WINDOW_S,
            "nfRate": nf / WINDOW_S,
            "netErrRate": nete / WINDOW_S,
        })
    return pd.DataFrame(rows)

# ---- load data
files = glob.glob(os.path.join("data", "*.csv"))
rows = []

for f in files:
    label = infer_label(f)
    if label == "skip":
        continue
    df = load_csv(f)
    feats = to_window_features(df)
    feats["label"] = label
    rows.append(feats)

data = pd.concat(rows, ignore_index=True)

print("Total number of window samples:", len(data))
print("\nClass distribution:")
print(data["label"].value_counts())

feature_cols = ["reqRate", "failRate", "nfRate", "netErrRate"]
X = data[feature_cols].values.astype(np.float32)
y = (data["label"] == "attack").astype(int).values

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.25, random_state=42, stratify=y
)

clf = LogisticRegression(max_iter=500, class_weight="balanced")
clf.fit(X_train, y_train)

pred = clf.predict(X_test)
print("Confusion matrix:\n", confusion_matrix(y_test, pred))
print(classification_report(y_test, pred))

w = clf.coef_[0]
b = clf.intercept_[0]
print("Weights:", w)
print("Bias:", b)

# export to header for ESP32
out_h = "ml_weights.h"
with open(out_h, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <math.h>\n\n")
    f.write("static const int ML_N = 4;\n")
    f.write("static const float ML_W[ML_N] = {")
    f.write(", ".join([f"{x:.8f}f" for x in w]))
    f.write("};\n")
    f.write(f"static const float ML_B = {b:.8f}f;\n\n")
    f.write("static inline float ml_sigmoid(float z){ return 1.0f/(1.0f+expf(-z)); }\n")
print("Saved:", out_h)