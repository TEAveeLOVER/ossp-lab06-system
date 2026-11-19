import csv, collections
import matplotlib.pyplot as plt

rows = list(csv.DictReader(open("../data/results.csv", encoding="utf-8")))
agg = collections.defaultdict(list)
for r in rows:
    agg[r["config"]].append(r)

order = [c for c in ["A", "B", "V", "IND"] if c in agg]
n      = [int(agg[c][0]["count"]) for c in order]
read   = [sum(float(x["read_ms"]) for x in agg[c]) / len(agg[c]) for c in order]
export = [sum(float(x["export_ms"]) for x in agg[c]) / len(agg[c]) for c in order]
csvb   = [int(agg[c][0]["csv_bytes"]) / 1024 for c in order]
jsonb  = [int(agg[c][0]["json_bytes"]) / 1024 for c in order]

idx = sorted(range(len(n)), key=lambda i: n[i])
n      = [n[i] for i in idx]; read = [read[i] for i in idx]; export = [export[i] for i in idx]
csvb   = [csvb[i] for i in idx]; jsonb = [jsonb[i] for i in idx]

plt.figure(); plt.plot(n, read, "o-", label="Чтение (EvtQuery/EvtNext/EvtRender)")
plt.plot(n, export, "s-", label="Экспорт (CSV+JSON)")
plt.title("Зависимость времени выполнения от числа событий")
plt.xlabel("Число экспортируемых событий, шт."); plt.ylabel("Среднее время, мс")
plt.grid(True); plt.legend(); plt.savefig("../report/graph1_time.png", dpi=200)

plt.figure(); plt.plot(n, csvb, "o-", label="CSV"); plt.plot(n, jsonb, "s-", label="JSON")
plt.title("Объём экспортированных файлов")
plt.xlabel("Число экспортируемых событий, шт."); plt.ylabel("Размер файла, КБ")
plt.grid(True); plt.legend(); plt.savefig("../report/graph2_size.png", dpi=200)