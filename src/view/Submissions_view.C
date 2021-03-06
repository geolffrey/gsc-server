#include "Submissions_view.h"
#include "Date_time_edit.h"
#include "Partner_notifications_widget.h"
#include "../model/auth/User.h"
#include "../model/Assignment.h"
#include "../model/Exam_grade.h"
#include "../model/Submission.h"
#include "../Session.h"

#include <Wt/Dbo/Dbo.h>
#include <Wt/Dbo/Transaction.h>
#include <Wt/WApplication.h>
#include <Wt/WDateTime.h>
#include <Wt/WLocalDateTime.h>
#include <Wt/WPushButton.h>
#include <Wt/WTable.h>
#include <Wt/WText.h>

#include <boost/lexical_cast.hpp>

#include <vector>

void load_model(const Wt::Dbo::ptr<User>& user, Session& session,
                Submissions_view_model& result)
{
    Wt::Dbo::Transaction transaction(session);

    auto& submissions = result.submissions;

    for (const auto& submission : user->submissions()) {
        int index = submission->assignment()->number();
        while (index >= submissions.size())
            submissions.emplace_back();

        // Make sure this is loaded now.
        submission->user1()->name();

        submissions[index].submission  = submission;
        submissions[index].file_count  = submission->file_count();
        submissions[index].eval_status = submission->eval_status();
    }

    for (auto exam_grade : Exam_grade::find_by_user(user)) {
        result.exams.push_back(exam_grade);
    }

    transaction.commit();
}

Submissions_view_row::Submissions_view_row(
        const Submissions_view_model_item& model,
        Session& session,
        Wt::WTableRow* row)
        : model_(model),
          session_(session),
          row_(row)
{
    row->elementAt(NAME)->addNew<Wt::WText>(
            model_.submission->assignment()->name());
    status_ = row_->elementAt(STATUS)->addNew<Wt::WText>();
    grade_  = row_->elementAt(GRADE)->addNew<Wt::WText>();
    action_ = row_->elementAt(ACTION)->addNew<Wt::WPushButton>();

    action_->clicked().connect(this, &Submissions_view_row::action);
}

void Submissions_view_row::add_headings(Wt::WTableRow* row)
{
    row->elementAt(NAME)     ->addNew<Wt::WText>("Assignment");
    row->elementAt(STATUS)   ->addNew<Wt::WText>("Status");
    row->elementAt(DUE_DATE) ->addNew<Wt::WText>("Due");
    row->elementAt(EVAL_DATE)->addNew<Wt::WText>("Self-eval due");
    row->elementAt(GRADE)    ->addNew<Wt::WText>("Grade");
    row->elementAt(ACTION)   ->addNew<Wt::WText>("Action");
}

void Submissions_view_row::set_files_action(const char* title)
{
    action_->setText(title);
    action_url_ = model_.submission->url();
}

void Submissions_view_row::set_eval_action(const char* title)
{
    action_->setText(title);
    action_url_ = model_.submission->eval_url();
}

void Submissions_view_row::set_action_style_class(const char* style)
{
    action_->setStyleClass(style);
}

void Submissions_view_row::update()
{
    auto const now = Wt::WDateTime::currentDateTime();

    auto time_to = [&](const Wt::WDateTime& date) {
        return now.timeTo(date, std::chrono::seconds{2});
    };

    action_->show();

    Wt::WString status;
    switch (model_.submission->status()) {
        case Submission::Status::future:
            row_->setStyleClass("future");
            status += "Opens in ";
            status += time_to(model_.submission->assignment()->open_date());
            action_->hide();
            break;

        case Submission::Status::open:
        case Submission::Status::extended:
            set_files_action("Submit");
            if (model_.file_count == 0) {
                row_->setStyleClass("open");
                status += "Due in ";
                status += time_to(model_.submission->effective_due_date());
                set_action_style_class("btn-success");
            } else {
                row_->setStyleClass("open");
                status += "Submitted ";
                status += boost::lexical_cast<std::string>(model_.file_count);
                status += " file";
                if (model_.file_count > 1) status += "s";
                set_action_style_class("btn");
            }
            break;

        case Submission::Status::self_eval:
        case Submission::Status::extended_eval:
            switch (model_.eval_status) {
                case Submission::Eval_status::empty: {
                    row_->setStyleClass("self-eval needed");
                    status += "Self-eval due in ";
                    status += time_to(model_.submission->effective_eval_date());
                    set_eval_action("Start");
                    set_action_style_class("btn-success");
                    break;
                }

                case Submission::Eval_status::started: {
                    row_->setStyleClass("self-eval started");
                    status += "Self-eval due in ";
                    status += time_to(model_.submission->effective_eval_date());
                    set_eval_action("Continue");
                    set_action_style_class("btn-success");
                    break;
                }

                case Submission::Eval_status::complete: {

                    status += "Self-eval complete";
                    set_eval_action("Edit");
                    set_action_style_class("btn");
                    break;
                }
            }
            break;

        case Submission::Status::closed: {
            row_->setStyleClass("closed");
            status += "Closed ";
            status += model_.submission->effective_eval_date().timeTo(
                    now, std::chrono::seconds{2});
            status += " ago";
            set_eval_action("View");
            set_action_style_class("btn-link");
            {
                dbo::Transaction transaction(session_);
                if (model_.submission->is_graded()) {
                    grade_->setText(model_.submission->grade_string());
                } else if (! model_.submission->is_evaluated()) {
                    grade_->setText("0%");
                }
            }
            break;
        }
    }

    status_->setText(status);

    row_->elementAt(DUE_DATE)->setStyleClass(
            model_.submission->effective_due_date() < now ? "past" : "");
    row_->elementAt(EVAL_DATE)->setStyleClass(
            model_.submission->effective_eval_date() < now ? "past" : "");
}

void Submissions_view_row::action()
{
    if (!action_url_.empty())
        Wt::WApplication::instance()->setInternalPath(action_url_, true);
}

class Student_submissions_view_row : public Submissions_view_row
{
public:
    Student_submissions_view_row(
            const Submissions_view_model_item& model,
            Session& session,
            Wt::WTableRow* row);
};

Student_submissions_view_row::Student_submissions_view_row(
        const Submissions_view_model_item& model, Session& session,
        Wt::WTableRow* row)
        : Submissions_view_row(model, session, row)
{
    row_->elementAt(DUE_DATE)->addNew<Wt::WText>(
            model_.submission->effective_due_date().toLocalTime()
                  .toString("ddd, MMM d 'at' h:mm AP"));
    row_->elementAt(EVAL_DATE)->addNew<Wt::WText>(
            model_.submission->effective_eval_date().toLocalTime()
                  .toString("ddd, MMM d 'at' h:mm AP"));
}

class Admin_submissions_view_row : public Submissions_view_row
{
public:
    Admin_submissions_view_row(
            const Submissions_view_model_item& model,
            Session& session,
            Wt::WTableRow* row);

    virtual void update() override;

protected:
    virtual void set_files_action(const char*) override;
    virtual void set_eval_action(const char*) override;
    virtual void set_action_style_class(const char*) override;

    Date_time_edit* due_date_;
    Date_time_edit* eval_date_;

    void due_date_changed_();
    void eval_date_changed_();
};

Admin_submissions_view_row::Admin_submissions_view_row(
        const Submissions_view_model_item& model,
        Session& session,
        Wt::WTableRow* row)
        : Submissions_view_row(model, session, row)
{
    due_date_ = row->elementAt(DUE_DATE)->addNew<Date_time_edit>();
    eval_date_ = row->elementAt(EVAL_DATE)->addNew<Date_time_edit>();

    due_date_->set_date_format("M/d/yy");
    eval_date_->set_date_format("M/d/yy");

    due_date_->set_time_format("H:mm");
    eval_date_->set_time_format("H:mm");

    due_date_->changed().connect(this,
                                 &Admin_submissions_view_row::due_date_changed_);
    eval_date_->changed().connect(this,
                                  &Admin_submissions_view_row::eval_date_changed_);
}

void Admin_submissions_view_row::update()
{
    Submissions_view_row::update();
    due_date_->set_date_time(model_.submission->effective_due_date());
    eval_date_->set_date_time(model_.submission->effective_eval_date());

    if (model_.submission->extended()) {
        due_date_->setStyleClass("extended");
        auto date = model_.submission->assignment()->due_date().toLocalTime().toString();
        due_date_->setToolTip("Extended from " + date);
    } else {
        due_date_->setStyleClass("");
        due_date_->setToolTip("");
    }

    if (model_.submission->eval_extended()) {
        eval_date_->setStyleClass("extended");
        auto date = model_.submission->assignment()->eval_date().toLocalTime().toString();
        eval_date_->setToolTip("Extended from " + date);
    } else {
        eval_date_->setStyleClass("");
        eval_date_->setToolTip("");
    }
}

void Admin_submissions_view_row::due_date_changed_()
{
    if (due_date_->validate() == Wt::ValidationState::Valid) {
        due_date_->setStyleClass("");
        Wt::Dbo::Transaction transaction(session_);
        model_.submission.modify()->set_due_date(due_date_->date_time());
        transaction.commit();
        update();
    } else {
        due_date_->setStyleClass("invalid");
    }
}

void Admin_submissions_view_row::eval_date_changed_()
{
    if (eval_date_->validate() == Wt::ValidationState::Valid) {
        eval_date_->setStyleClass("");
        Wt::Dbo::Transaction transaction(session_);
        model_.submission.modify()->set_eval_date(eval_date_->date_time());
        transaction.commit();
        update();
    } else {
        eval_date_->setStyleClass("invalid");
    }
}

void Admin_submissions_view_row::set_files_action(const char*)
{
    Submissions_view_row::set_files_action("Files");
}

void Admin_submissions_view_row::set_eval_action(const char*)
{
    Submissions_view_row::set_eval_action("Eval");
}

void Admin_submissions_view_row::set_action_style_class(const char*)
{
    Submissions_view_row::set_action_style_class("btn");
}

std::unique_ptr<Submissions_view_row>
Submissions_view_row::construct(const Submissions_view_model_item& model,
                                Session& session, Wt::WTableRow* row)
{
    if (session.user()->can_admin()) {
        auto result = std::make_unique<Admin_submissions_view_row>(
                model, session, row);
        result->update();
        return std::move(result);
    } else {
        auto result = std::make_unique<Student_submissions_view_row>(
                model, session, row);
        result->update();
        return std::move(result);
    }
}

Submissions_view::Submissions_view(const Wt::Dbo::ptr<User>& user, Session& session)
        : session_(session),
          model_(std::make_unique<Submissions_view_model>())
{
    setStyleClass("submissions-view");

    addNew<Partner_notification_widget>(user, dbo::ptr<Submission>{}, session, dummy_);

    load_model(user, session, *model_);

    auto table = addNew<Wt::WTable>();
    table->setHeaderCount(1);
    Submissions_view_row::add_headings(table->rowAt(0));

    int row = 1;
    for (const auto& each : model_->submissions) {
        if (!each.submission) continue;
        rows_.push_back(Submissions_view_row::construct(each, session,
                                                        table->rowAt(row++)));
    }

    auto exam_table = addNew<Wt::WTable>();
    exam_table->setStyleClass("exam-table");
    row = 0;
    for (const auto& each : model_->exams) {
        auto exam_row = exam_table->rowAt(row++);

        std::ostringstream fmt;
        fmt << "Exam " << each->number();
        exam_row->elementAt(0)->addNew<Wt::WText>(fmt.str());

        fmt.str("");
        fmt << each->points() << " / " << each->possible();
        exam_row->elementAt(1)->addNew<Wt::WText>(fmt.str());

        exam_row->elementAt(2)->addNew<Wt::WText>(each->pct_string());
    }
}
