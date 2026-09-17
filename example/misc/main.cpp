#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/LogStream>
#include <AsyncFw/Cache>

// Структура данных, которую мы кэшируем
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

  // Обновляем поля прямо на месте (in-place) под защитой LockedData
  {
    auto config = configCache.acquire();
    config->ipAddress = "192.168.1.50";
    config->port = 5432;
    config->maxConnections = 200;
  }

  logNotice() << "[Worker] Данные успешно обновлены в Cache!";
}

int main(int argc, char *argv[]) {
  // 1. Инициализируем глобальный пул потоков приложения
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CacheExamplePool");

  // 2. Создаем кэш на стеке с TTL = 1 секунда (1000 мс).
  // Передаем функцию обновления прямо в конструктор кэша.
  // Используем ленивый захват &configCache через указатель или ссылку
  AsyncFw::Cache<DatabaseConfig> configCache(1000, [&configCache]() {
    logNotice() << "[Main/Callback] Триггер обновления сработал! Делегируем задачу в ThreadPool...";

    // Отправляем тяжелую работу в фоновый пул
    AsyncFw::ThreadPool::async([&configCache]() {
      fetchFreshConfigFromServer(configCache);
    }, [&configCache]() {
      logNotice() << "[Main/Callback] Фоновая задача обновления полностью завершена.";
      {
        // Получаем объект LockedData. 0 копирований!
        auto wrapped = configCache.acquire();
        logInfo() << "Config IP:" << wrapped->ipAddress << "Port:" << wrapped->port;
      }
      AsyncFw::MainThread::exit(0); // Завершаем приложение
    });
  });

  // Задаем первоначальные (stale) данные
  configCache.store({"127.0.0.1", 3306, 50});

  logNotice() << "Шаг 1: Запрашиваем свежие данные (TTL еще не вышел)";
  {
    auto wrapped = configCache.acquire();
    logInfo() << "Config IP:" << wrapped->ipAddress << "Port:" << wrapped->port;
  }

  // Имитируем прошествие времени — спим 1.5 секунды, чтобы кэш гарантированно устарел
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  logNotice() << "Шаг 2: Запрашиваем данные снова (Кэш устарел, триггерится обновление)";
  {
    // Метод видит, что TTL истек, сбрасывает токен и вызывает сохраненную лямбду.
    // Возвращает старые (stale) данные мгновенно.
    auto wrapped = configCache.acquire();
    logInfo() << "Stale Config IP:" << wrapped->ipAddress << "Port:" << wrapped->port;
  }

  logNotice() << "Шаг 3: Входим в главный цикл обработки событий...";
  // Запускаем цикл MainThread. Здесь выполнится асинхронный коллбэк пула потоков
  int ret = AsyncFw::MainThread::exec();

  logNotice() << "Конец приложения с кодом:" << ret;
  return ret;
}
