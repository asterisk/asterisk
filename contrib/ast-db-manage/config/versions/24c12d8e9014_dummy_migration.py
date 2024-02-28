"""dummy migration

This migration exists to ensure that all currently supported branches
have the same alembic revision head. This makes managing migrations
across supported branches less painful.

The migration that is stubbed here is:

    https://github.com/asterisk/asterisk/commit/775352ee6c2a5bcd4f0e3df51aee5d1b0abf4cbe

Revision ID: 24c12d8e9014
Revises: 37a5332640e2
Create Date: 2024-01-05 14:14:47.510917

"""

# revision identifiers, used by Alembic.
revision = '24c12d8e9014'
down_revision = '37a5332640e2'

def upgrade():
    pass

def downgrade():
    pass
