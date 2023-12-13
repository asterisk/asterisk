"""add user-agent-header to ps_registrations

Revision ID: 24c12d8e9014
Revises: 37a5332640e2
Create Date: 2024-01-05 14:14:47.510917

"""

# revision identifiers, used by Alembic.
revision = '24c12d8e9014'
down_revision = '37a5332640e2'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_registrations', sa.Column('user_agent', sa.String(255)))


def downgrade():
    op.drop_column('ps_registrations', 'user_agent')
