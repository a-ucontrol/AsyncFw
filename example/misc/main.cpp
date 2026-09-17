#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Cache>
#include <AsyncFw/LogStream>

// Огромная структура данных, которую мы кэшируем
struct DatabaseConfig {
  std::string ipAddress;
  int port;
  int maxConnections;
};

// Функция-воркер для имитации тяжелого запроса в сеть или БД
void fetchFreshConfigFromServer(AsyncFw::Cache<DatabaseConfig>& configCache) {
  auto *ct = AsyncFw::AbstractThread::current();
  logNotice() << "[Worker] Начинаем тяжелую загрузку данных в потоке:" << ct->name();

  // Имитируем долгую сетевую задержку (например, 2 секунды)
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Обновляем отдельные поля прямо на месте (in-place) под защитой мьютекса

  {
    auto locked = configCache.acquire();
    auto &config = locked.data();

    config.ipAddress = "192.168.1.50";
    config.port = 5432;
    config.maxConnections = 200;
  }

  logNotice() << "[Worker] Данные успешно обновлены в Cache!";
}

int main(int argc, char *argv[]) {
  // 1. Инициализируем глобальный пул потоков приложения
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CacheExamplePool");

  // 2. Создаем кэш на стеке с TTL = 1 секунда (1000 мс)
  AsyncFw::Cache<DatabaseConfig> configCache(1000);

  // Задаем первоначальные (stale) данные
  configCache.store({"127.0.0.1", 3306, 50});

  // 3. Подписываемся на сигнал устаревания кэша.
  // Из-за политики QueuedOnly лямбда вызовется асинхронно в MainThread,
  // когда текущий поток отпустит мьютекс и вернется в event loop.
  configCache.update.connect([&configCache]() {
    logNotice() << "[Main] Сигнал update сработал! Делегируем задачу в ThreadPool...";

    // Отправляем тяжелую работу в фоновый поток
    AsyncFw::ThreadPool::async([&configCache]() {
      fetchFreshConfigFromServer(configCache);
    }, [&configCache]() {
      logNotice() << "[Main] Фоновая задача обновления полностью завершена.";
      {
        // Получаем объект Locked. 0 копирований!
        auto wrapped = configCache.acquire();

        // Доступ к данным через .data(), который возвращает const DatabaseConfig&
        logInfo() << "Config IP:" << wrapped.data().ipAddress << "Port:" << wrapped.data().port;
      } // Мьютекс кэша гарантированно освобожден здесь
      AsyncFw::MainThread::exit(0); // Завершаем приложение
    });
  });

  logNotice() << "Шаг 1: Запрашиваем свежие данные (TTL еще не вышел)";
  {
    // Получаем объект Locked. 0 копирований!
    auto wrapped = configCache.acquire();

    // Доступ к данным через .data(), который возвращает const DatabaseConfig&
    logInfo() << "Config IP:" << wrapped.data().ipAddress << "Port:" << wrapped.data().port;
  } // Мьютекс кэша гарантированно освобожден здесь

  // Имитируем прошествие времени — спим 1.5 секунды, чтобы кэш железно устарел
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  logNotice() << "Шаг 2: Запрашиваем данные снова (Кэш устарел, триггерится обновление)";
  {
    // Этот вызов видит, что TTL истек, сбрасывает токен и вызывает update().
    // Поскольку update() работает по политике QueuedOnly, Deadlock исключен.
    // Метод мгновенно возвращает старые (stale) данные.
    auto wrapped = configCache.acquire();
    logInfo() << "Stale Config IP:" << wrapped.data().ipAddress << "Port:" << wrapped.data().port;
  }

  logNotice() << "Шаг 3: Входим в главный цикл обработки событий...";
  // Запускаем цикл MainThread. Здесь обработается сгенерированный сигнал update
  int ret = AsyncFw::MainThread::exec();

  logNotice() << "Конец приложения с кодом:" << ret;
  return ret;
}
