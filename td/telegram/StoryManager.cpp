//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/Td.h"

namespace td {

StoryManager::StoryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

StoryManager::~StoryManager() = default;

void StoryManager::tear_down() {
  parent_.reset();
}

bool StoryManager::is_local_story_id(StoryId story_id) {
  return story_id.get() < 0;
}

const StoryManager::Story *StoryManager::get_story(StoryId story_id) const {
  return stories_.get_pointer(story_id);
}

StoryManager::Story *StoryManager::get_story_editable(StoryId story_id) {
  return stories_.get_pointer(story_id);
}

StoryId StoryManager::on_get_story(DialogId owner_dialog_id,
                                   telegram_api::object_ptr<telegram_api::storyItem> &&story_item) {
  CHECK(story_item != nullptr);
  StoryId story_id(story_item->id_);
  if (!story_id.is_valid() || is_local_story_id(story_id)) {
    return StoryId();
  }

  auto story = get_story_editable(story_id);
  bool is_changed = false;
  bool need_save_to_database = false;
  if (story == nullptr) {
    auto s = make_unique<Story>();
    story = s.get();
    stories_.set(story_id, std::move(s));
  }
  CHECK(story != nullptr);

  bool is_bot = td_->auth_manager_->is_bot();
  auto caption =
      get_message_text(td_->contacts_manager_.get(), std::move(story_item->caption_), std::move(story_item->entities_),
                       true, is_bot, story_item->date_, false, "on_get_story");
  auto content = get_story_content(td_, std::move(story_item->media_), owner_dialog_id);
  if (content == nullptr) {
    return StoryId();
  }
  auto content_type = content->get_type();

  auto privacy_rules = UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(story_item->privacy_));

  vector<UserId> recent_viewer_user_ids;
  int32 view_count = 0;
  if (story_item->views_ != nullptr) {
    for (auto &viewer_id : story_item->views_->recent_viewers_) {
      UserId user_id(viewer_id);
      if (user_id.is_valid() && td_->contacts_manager_->have_min_user(user_id)) {
        recent_viewer_user_ids.push_back(user_id);
      } else {
        LOG(ERROR) << "Receive " << user_id << " as recent viewer in " << story_id;
      }
    }
    view_count = story_item->views_->views_count_;
  }

  if (story->is_pinned_ != story_item->pinned_ || story->is_public_ != story_item->public_ ||
      story->is_for_close_friends_ != story_item->close_friends_ || story->date_ != story_item->date_ ||
      story->expire_date_ != story_item->expire_date_ || !(story->privacy_rules_ == privacy_rules) ||
      story->recent_viewer_user_ids_ != recent_viewer_user_ids || story->view_count_ != view_count ||
      story->caption_ != caption) {
    story->is_pinned_ = story_item->pinned_;
    story->is_public_ = story_item->public_;
    story->is_for_close_friends_ = story_item->close_friends_;
    story->date_ = story_item->date_;
    story->expire_date_ = story_item->expire_date_;
    story->privacy_rules_ = std::move(privacy_rules);
    story->recent_viewer_user_ids_ = std::move(recent_viewer_user_ids);
    story->view_count_ = view_count;
    story->caption_ = std::move(caption);
    is_changed = true;
  }
  if (story->content_ == nullptr || story->content_->get_type() != content_type) {
    story->content_ = std::move(content);
    is_changed = true;
  } else {
    merge_story_contents(td_, story->content_.get(), content.get(), owner_dialog_id, false, need_save_to_database,
                         is_changed);
  }

  if (is_changed || need_save_to_database) {
    // save_story(story, story_id);
  }
  if (is_changed) {
    // send_closure(G()->td(), &Td::send_update, td_api::make_object<td_api::updateStory>(get_story_object(story_id, story)));
  }
  return story_id;
}

}  // namespace td
