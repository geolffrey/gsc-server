#include "Grading_view.h"
#include "Unit_line_edit.h"
#include "Explanation_edit_widget.h"
#include "Explanation_view_widget.h"
#include "../Navigate.h"
#include "../model/Self_eval.h"
#include "../model/Eval_item.h"
#include "../model/Grader_eval.h"
#include "../model/Assignment.h"
#include "../Session.h"

#include <Wt/Dbo/ptr.h>
#include <Wt/WApplication.h>
#include <Wt/WCompositeWidget.h>
#include <Wt/WContainerWidget.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WSignal.h>
#include <Wt/WTemplate.h>
#include <Wt/WText.h>

#include <boost/lexical_cast.hpp>

// Abstract base class for grading self evaluations. Derived classes specialize
// for boolean-type evaluations and scale-type evaluations (including
// informational).
class Abstract_grading_widget : public Wt::WCompositeWidget
{
public:
    Abstract_grading_widget(const dbo::ptr<Grader_eval>&, Session&);

    // Creates an object of the correct derived class as determined by the
    // Type of the Grader_eval.
    static std::unique_ptr<Abstract_grading_widget>
    create(const dbo::ptr<Grader_eval>&, Session&);

    virtual ~Abstract_grading_widget();

protected:
    // The Grader_eval we are editing.
    dbo::ptr<Grader_eval> model_;
    Session& session_;

    Wt::WContainerWidget* buttons_;
    Wt::WPushButton* hold_button_ = nullptr;

    // Adds the hold_button_ to buttons_. Derived classes should call after
    // adding anything to buttons_ that should appear first.
    void add_hold_button();

    // Saves the Grader_eval in held-back status with score 0.
    virtual void hold_action_();

    // Saves the Grader_eval with the given status and score.
    void save_(Grader_eval::Status, double);

private:
    Explanation_edit_widget* explanation_;
};

// For grading boolean questions.
class Boolean_grading_widget : public Abstract_grading_widget
{
public:
    Boolean_grading_widget(const dbo::ptr<Grader_eval>&, Session&);

private:
    void yes_action_();
    void no_action_();
};

// For grading scale and informational questions.
class Scale_grading_widget : public Abstract_grading_widget
{
public:
    Scale_grading_widget(const dbo::ptr<Grader_eval>&, Session&);

protected:
    // Saves the Grader_eval in held-back status with current score.
    void hold_action_() override;

private:
    Unit_line_edit* edit_;
    Wt::WPushButton* apply_button_;

    void disable_buttons_();
    void enable_buttons_();

    // Saves the Grader_eval in ready status with current score.
    void apply_action_();

    // Saves the Grader_eval in the given status with current score.
    void save_(Grader_eval::Status);
};

std::unique_ptr<Abstract_grading_widget>
Abstract_grading_widget::create(const dbo::ptr<Grader_eval>& grader_eval, Session& session)
{
    switch (grader_eval->self_eval()->eval_item()->type()) {
        case Eval_item::Type::Boolean:
            return std::make_unique<Boolean_grading_widget>(grader_eval, session);
        case Eval_item::Type::Scale:
        case Eval_item::Type::Informational:
            return std::make_unique<Scale_grading_widget>(grader_eval, session);
    }
}

Abstract_grading_widget::Abstract_grading_widget(const dbo::ptr<Grader_eval>& model,
                                                 Session& session)
        : model_(model),
          session_(session)
{
    auto impl = setNewImplementation<Wt::WContainerWidget>();

    explanation_ = impl->addNew<Explanation_edit_widget>();
    explanation_->setText(model->explanation());
    explanation_->setFocus();

    buttons_ = impl->addNew<Wt::WContainerWidget>();
    buttons_->setStyleClass("buttons");
}

void Abstract_grading_widget::add_hold_button()
{
    hold_button_ = buttons_->addNew<Wt::WPushButton>("Hold");
    hold_button_->clicked().connect(this,
                                    &Abstract_grading_widget::hold_action_);
}

Abstract_grading_widget::~Abstract_grading_widget()
{
    if (model_->status() == Grader_eval::Status::editing) {
        dbo::Transaction transaction(session_);
        model_.remove();
    }
}

void Abstract_grading_widget::hold_action_()
{
    save_(Grader_eval::Status::held_back, 0);
}

void Abstract_grading_widget::save_(Grader_eval::Status status, double score)
{
    dbo::Transaction transaction(session_);
    auto grader_eval = model_.modify();
    grader_eval->set_status(status);
    grader_eval->set_score(score);
    grader_eval->set_explanation(explanation_->text().toUTF8());
    grader_eval->set_grader(session_.user());

    Navigate::to("/grade");
}

Boolean_grading_widget::Boolean_grading_widget(const dbo::ptr<Grader_eval>& model,
                                               Session& session)
        : Abstract_grading_widget(model, session)
{
    auto yes_button = buttons_->addNew<Wt::WPushButton>("Yes");
    yes_button->clicked().connect(this, &Boolean_grading_widget::yes_action_);
    auto no_button = buttons_->addNew<Wt::WPushButton>("No");
    no_button->clicked().connect(this, &Boolean_grading_widget::no_action_);

    add_hold_button();
}

void Boolean_grading_widget::yes_action_()
{
    save_(Grader_eval::Status::ready, 1);
}

void Boolean_grading_widget::no_action_()
{
    save_(Grader_eval::Status::ready, 0);
}

Scale_grading_widget::Scale_grading_widget(const dbo::ptr<Grader_eval>& model, Session& session)
        : Abstract_grading_widget(model, session)
{
    edit_ = buttons_->addNew<Unit_line_edit>();
    edit_->set_value(model->score());
    edit_->setStyleClass("unit-edit");

    apply_button_ = buttons_->addNew<Wt::WPushButton>("Apply");
    apply_button_->clicked().connect(this,
                                     &Scale_grading_widget::apply_action_);

    add_hold_button();

    edit_->valid().connect(this, &Scale_grading_widget::enable_buttons_);
    edit_->invalid().connect(this, &Scale_grading_widget::disable_buttons_);
}

void Scale_grading_widget::disable_buttons_()
{
    hold_button_->disable();
    apply_button_->disable();
}

void Scale_grading_widget::enable_buttons_()
{
    hold_button_->enable();
    apply_button_->enable();
}

void Scale_grading_widget::apply_action_()
{
    save_(Grader_eval::Status::ready);
}

void Scale_grading_widget::hold_action_()
{
    save_(Grader_eval::Status::held_back);
}

void Scale_grading_widget::save_(Grader_eval::Status status)
{
    double score = edit_->value();

    if (score == Unit_line_edit::INVALID) {
        edit_->set_value(score);
        edit_->setFocus();
    } else {
        Abstract_grading_widget::save_(status, score);
    }
}

Grading_view::Grading_view(const Wt::Dbo::ptr<Self_eval> self_eval, Session& session)
        : Abstract_file_view(self_eval->submission(), session),
          model_(self_eval)
{
    auto widget = right_column_->addNew<Wt::WTemplate>(
        "<div class='grading-view'>"
          "<h4>Question ${sequence} <small>${homework}</small></h4>"
          "<div class='question'>${question}</div>"
          "<h5>Self evaluation</h5>"
          "<p class='answer'><strong>${self_grade}.</strong> "
            "<span>${self_explanation}</span></p>"
          "<h5>Your evaluation</h5>"
          "${grading_widget}"
          "<p class='status'>Status: ${status}</p>"
        "</div>"
    );

    dbo::Transaction transaction(session_);
    auto eval_item = self_eval->eval_item();
    auto assignment = eval_item->assignment();
    auto grader_eval = Grader_eval::get_for(self_eval, session_);

    std::string sequence =
            boost::lexical_cast<std::string>(eval_item->sequence());
    auto grading_widget = Abstract_grading_widget::create(grader_eval,
                                                          session_);

    std::string status;
    switch (grader_eval->status()) {
        case Grader_eval::Status::editing:
            status += "unsaved";
            break;
        case Grader_eval::Status::held_back:
            status += "held";
            break;
        case Grader_eval::Status::ready:
            status += "saved (";
            status += Eval_item::pct_string(grader_eval->score());
            status += ")";
            break;
    }

    widget->bindWidget("sequence", std::make_unique<Wt::WText>(sequence));
    widget->bindWidget("homework", std::make_unique<Wt::WText>(assignment->name()));
    widget->bindWidget("question", std::make_unique<Wt::WText>(eval_item->prompt()));
    widget->bindWidget("self_grade",
                       std::make_unique<Wt::WText>(
                               eval_item->format_score(self_eval->score())));
    widget->bindWidget("self_explanation",
                       std::make_unique<Explanation_view_widget>(
                               self_eval->explanation(),
                               file_viewer(),
                               "student-highlight"));
    widget->bindWidget("grading_widget", std::move(grading_widget));
    widget->bindWidget("status", std::make_unique<Wt::WText>(status));
}

