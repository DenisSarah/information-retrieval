# Поиск по MS MARCO

Свой обратный индекс и булев поиск на C++17. Датасет — [MS MARCO V2.1 Document Corpus](https://trec-rag.github.io/annoucements/2025-rag25-corpus/), 10 960 555 документов.

Нужны macOS/Linux, компилятор C++17, `make` и zlib. Все команды — из папки проекта. Корпус и индекс в Git не входят.

Подготовка индекса: скачай TAR по ссылке выше и выполни команды. Если `build/msmarco.idx` уже есть, переходи к поиску.

```sh
make
mkdir -p data/msmarco
tar -xf /path/to/msmarco_v2.1_doc.tar -C data/msmarco
./build/search index data/msmarco build/msmarco.idx 256
```

Внутренние `.gz` распаковывать не нужно. `256` — бюджет накопления сегмента в MiB. Индекс строится порциями и читается через `mmap`.

Поиск в браузере:

```sh
make web
```

Открыть [localhost:8000](http://127.0.0.1:8000). Остановить сервер — Ctrl+C. Есть общее число совпадений, лимит выдачи, ссылки и сохранённые тексты. Для исходных ссылок нужны файлы корпуса в `data/msmarco/msmarco_v2.1_doc/`.

Поиск в терминале:

```sh
make
./build/search search build/msmarco.idx 'dijkstra AND NOT algorithm' 3
./build/search shell build/msmarco.idx
```

В `shell` вводить запросы по одному; выход — `:quit`. Операторы `AND`, `OR`, `NOT` — заглавными; работают скобки, между словами подразумевается AND. Результаты идут по docID. Ранжирования, позиций слов и сжатия пока нет.

Проверки:

```sh
make test
make sanitize
./build/search verify build/msmarco.idx
```
