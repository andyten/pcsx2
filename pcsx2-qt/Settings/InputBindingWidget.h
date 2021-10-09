#pragma once
#include "common/Pcsx2Defs.h"
#include "pcsx2/Frontend/InputManager.h"
#include <QtWidgets/QPushButton>
#include <optional>

class QTimer;

class QtHostInterface;

class InputBindingWidget : public QPushButton
{
  Q_OBJECT

public:
  InputBindingWidget(std::string section_name, std::string key_name, QWidget* parent);
  ~InputBindingWidget();

  __fi InputBindingWidget* getNextWidget() const { return m_next_widget; }
  __fi void setNextWidget(InputBindingWidget* widget) { m_next_widget = widget; }

public Q_SLOTS:
  void beginRebindAll();
  void clearBinding();
  void reloadBinding();

protected Q_SLOTS:
  void onClicked();
  void onInputListenTimerTimeout();
  void inputManagerHookCallback(InputBindingKey key, float value);

protected:
  enum : u32
  {
    TIMEOUT_FOR_SINGLE_BINDING = 5,
    TIMEOUT_FOR_ALL_BINDING = 10
  };

  virtual bool eventFilter(QObject* watched, QEvent* event) override;
  virtual bool event(QEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* e) override;

  virtual void startListeningForInput(u32 timeout_in_seconds);
  virtual void stopListeningForInput();
  virtual void openDialog();

  bool isListeningForInput() const { return m_input_listen_timer != nullptr; }
  void setNewBinding();
  void updateText();

  void hookInputManager();
  void unhookInputManager();

  std::string m_section_name;
  std::string m_key_name;
  std::vector<std::string> m_bindings;
  std::vector<InputBindingKey> m_new_bindings;
  QTimer* m_input_listen_timer = nullptr;
  u32 m_input_listen_remaining_seconds = 0;

  InputBindingWidget* m_next_widget = nullptr;
  bool m_is_binding_all = false;
};
